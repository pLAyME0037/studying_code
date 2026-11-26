// Master-child engine: port of webc_pos src/layout/master_child.c (data
// loading + search/where), the six page configs (src/pos/geo.c +
// src/pos/pos.c dictionaries) and the SERVE_* write macros
// (src/core/webc_template.h) shared by the geo4 + dictionaries routes.
//
// Slice-6 deviations from webc (user-approved, recorded in PLAN.md):
//   * forms gain the id-code + FK fields webc's write handlers require but
//     its display-column forms never rendered (webc geo4 create/update
//     always answers 400 — see PLAN.md "webc bugs found");
//   * child add/edit rows post their FK as a body field from a select
//     scoped to the master row (webc posts fk=<master id> in the query —
//     the wrong parent on every subquery tab);
//   * child tabs load lazily (?child=<table>&master=<id>&fragment=1) —
//     webc eagerly renders every child window for every master row.
import type { NextRequest } from "next/server";
import { db, queryAll, queryOne } from "./db";
import {
  dbRun,
  errorPage,
  formText,
  idValid,
  isUniqueMsg,
  redirect302,
  uniquePage,
} from "./crud";
import {
  type PageInfo,
  type Query,
  firstOf,
  pageInfoFinish,
  pageInfoParse,
  pageInfoParseSession,
} from "./paging";

// ---- config types -------------------------------------------------------

// One form input. `display` columns also render in the thead, the query
// filter row and the data cells; fields without it are form-only (the
// id-code + FK columns webc's write handlers require).
export type MdField = {
  name: string;
  label: string;
  display?: boolean;
  num?: boolean; // input type=number step=any (COL_TYPE_NUM)
  required?: boolean; // HTML required (webc: !nullable && type != DATE)
  fk?: {
    optionsSql: string; // ?-bound (or parameterless) id, label pairs
    table: string; // label fallback lookup: SELECT <labelCol> FROM table
    labelCol: string;
  };
};

export type MdChild = {
  table: string;
  title: string;
  fkColumn: string; // child column pointing at the master
  fkLabel: string; // label of our scoped FK select (aria-label)
  // webc md_child_where's fk_subquery with %s -> ? (master id binds as a
  // parameter); NULL = direct "<fk> = ?" match.
  fkCondition: string | null;
  crudPath: string;
  columns: MdField[]; // child display columns (thead + cells)
  softDelete: boolean; // MD_ChildTab.soft_delete — 0 for all three geo
  // children, so the tab never filters on deleted_at (md_child_where).
  // Form-only id-code column (dist_id/comm_id): webc's child add/edit rows
  // render the display columns only, so its writes 400 — the port adds it.
  codeField: MdField | null;
  // Scoped parent options for our add/edit-row FK select (fixes webc's
  // wrong-parent query fk). Always bound with the master id.
  fkOptionsSql: string;
  fkTable: string;
  fkLabelCol: string;
};

export type MdWrite = {
  singular: string; // 409 reason token: "A <singular> with the same ..."
  fallback: string; // redirect target when ?redirect= is missing/unsafe
  required: string[]; // body keys, ordered (missing key or empty [0] -> 400)
  optional: string[]; // body -> query -> "" (bound only if the SQL lists them)
  cols: string[]; // INSERT / UPDATE bind order (webc's SQL order)
  numKeys: string[]; // INSERT only: "" -> 0 (webc pos_util.h sql_num)
  softDelete: boolean; // /delete + /restore routes exist
};

export type MdConfig = {
  table: string;
  title: string;
  path: string; // md_path — the list URL (no query)
  crudPath: string; // write-route prefix
  fields: MdField[]; // FORM order (webc fields[] + optional FK, write order)
  softDelete: boolean;
  readOnly: boolean; // no actions, no create form (villages, geo)
  createOnly: boolean; // create form + rows, no row actions (dictionaries)
  children: MdChild[];
  write: MdWrite | null; // null = GET-only page (webc registers no POSTs)
};

// A loaded master/child row: id + every selected column by name. webc
// keeps parallel values/disp arrays; all six configs have plain text
// columns (no DATE/FK display), so disp == values and one map suffices.
export type MdRow = { id: string; data: Record<string, string> };

export type MdSearchTerm = {
  col: string | null; // scoped column; null = every display column
  tokens: string[]; // each bound as "%token%"
};

// ---- configs ------------------------------------------------------------

// src/pos/geo.c md_provinces_columns + serve_pos_provinces children
// (districts direct; communes/villages via subquery). formFields follows
// geo.c's prov_fields[] write order; the FK options are our scoped-select
// fix (webc posts province_id/district_id in the query string instead).
export const MD_PROVINCES: MdConfig = {
  table: "provinces",
  title: "Provinces",
  path: "/pos/provinces",
  crudPath: "/pos/provinces",
  softDelete: true,
  readOnly: false,
  createOnly: false,
  fields: [
    { name: "prov_id", label: "Code", required: true },
    { name: "name_kh", label: "Khmer Name", display: true, required: true },
    { name: "name_en", label: "English Name", display: true, required: true },
  ],
  children: [
    {
      table: "districts",
      title: "Districts",
      fkColumn: "province_id",
      fkLabel: "Province",
      fkCondition: null,
      crudPath: "/pos/districts",
      softDelete: false,
      codeField: { name: "dist_id", label: "Code", required: true },
      columns: [
        { name: "type", label: "Type", display: true, required: true },
        { name: "name_kh", label: "Khmer Name", display: true, required: true },
        { name: "name_en", label: "English Name", display: true, required: true },
      ],
      fkOptionsSql: "SELECT id, name_kh FROM provinces WHERE id = ?;",
      fkTable: "provinces",
      fkLabelCol: "name_kh",
    },
    {
      table: "communes",
      title: "Communes",
      fkColumn: "district_id",
      fkLabel: "District",
      // webc geo.c fk_subquery: district_id IN (SELECT id FROM districts
      // WHERE province_id = '%s')
      fkCondition: "district_id IN (SELECT id FROM districts WHERE province_id = ?)",
      crudPath: "/pos/communes",
      softDelete: false,
      codeField: { name: "comm_id", label: "Code", required: true },
      columns: [
        { name: "name_kh", label: "Khmer Name", display: true, required: true },
        { name: "name_en", label: "English Name", display: true, required: true },
      ],
      fkOptionsSql:
        "SELECT id, name_kh FROM districts WHERE province_id = ? ORDER BY name_kh ASC;",
      fkTable: "districts",
      fkLabelCol: "name_kh",
    },
    {
      table: "villages",
      title: "Villages",
      fkColumn: "commune_id",
      fkLabel: "Commune",
      // webc geo.c: commune_id IN (SELECT c.id FROM communes c JOIN
      // districts d ON c.district_id = d.id WHERE d.province_id = '%s')
      fkCondition:
        "commune_id IN (SELECT c.id FROM communes c " +
        "JOIN districts d ON c.district_id = d.id WHERE d.province_id = ?)",
      crudPath: "/pos/villages",
      softDelete: false,
      // GET-only child (no /pos/villages write routes): no code input, no
      // FK select — mdChildWrite() is null and the tab stays display-only.
      codeField: null,
      columns: [
        { name: "name_kh", label: "Khmer Name", display: true, required: true },
        { name: "name_en", label: "English Name", display: true, required: true },
      ],
      fkOptionsSql:
        "SELECT c.id, c.name_kh FROM communes c " +
        "JOIN districts d ON c.district_id = d.id " +
        "WHERE d.province_id = ? ORDER BY c.name_kh ASC;",
      fkTable: "communes",
      fkLabelCol: "name_kh",
    },
  ],
  // geo.c prov_fields[] = { prov_id, name_kh, name_en } (+ created_at as
  // an ignored OPT — never bound, so it is not extracted here).
  write: {
    singular: "province",
    fallback: "/pos_provinces",
    required: ["prov_id", "name_kh", "name_en"],
    optional: [],
    cols: ["prov_id", "name_kh", "name_en"],
    numKeys: [],
    softDelete: true,
  },
};

// geo.c md_districts_columns + dist_fields[] { dist_id, name_kh, name_en,
// type } + opt { province_id } — the FK select is our form-only fix.
export const MD_DISTRICTS: MdConfig = {
  table: "districts",
  title: "Districts",
  path: "/pos/districts",
  crudPath: "/pos/districts",
  softDelete: true,
  readOnly: false,
  createOnly: false,
  // Form order: the id-code leads the form; the display fields keep webc's
  // md_districts_columns order (type, name_kh, name_en) so the thead and
  // the query row come out webc-identical (mdDisplayCols filters on
  // `display`, preserving relative order).
  fields: [
    { name: "dist_id", label: "Code", required: true },
    { name: "type", label: "Type", display: true, required: true },
    { name: "name_kh", label: "Khmer Name", display: true, required: true },
    { name: "name_en", label: "English Name", display: true, required: true },
    {
      name: "province_id",
      label: "Province",
      required: true,
      fk: {
        optionsSql:
          "SELECT id, name_kh FROM provinces WHERE deleted_at IS NULL ORDER BY name_kh ASC;",
        table: "provinces",
        labelCol: "name_kh",
      },
    },
  ],
  children: [],
  write: {
    singular: "district",
    fallback: "/pos_districts",
    required: ["dist_id", "name_kh", "name_en", "type"],
    optional: ["province_id"],
    cols: ["dist_id", "name_kh", "name_en", "type", "province_id"],
    numKeys: [],
    softDelete: true,
  },
};

// geo.c md_communes_columns + comm_fields[] { comm_id, name_kh, name_en }
// + opt { district_id }.
export const MD_COMMUNES: MdConfig = {
  table: "communes",
  title: "Communes",
  path: "/pos/communes",
  crudPath: "/pos/communes",
  softDelete: true,
  readOnly: false,
  createOnly: false,
  fields: [
    { name: "comm_id", label: "Code", required: true },
    { name: "name_kh", label: "Khmer Name", display: true, required: true },
    { name: "name_en", label: "English Name", display: true, required: true },
    {
      name: "district_id",
      label: "District",
      required: true,
      fk: {
        optionsSql:
          "SELECT id, name_kh FROM districts WHERE deleted_at IS NULL ORDER BY name_kh ASC;",
        table: "districts",
        labelCol: "name_kh",
      },
    },
  ],
  children: [],
  write: {
    singular: "commune",
    fallback: "/pos_communes",
    required: ["comm_id", "name_kh", "name_en"],
    optional: ["district_id"],
    cols: ["comm_id", "name_kh", "name_en", "district_id"],
    numKeys: [],
    softDelete: true,
  },
};

// geo.c serve_pos_villages: read_only catalog, GET route alone — no
// write config (webc answers 404 on every /pos/villages POST too).
export const MD_VILLAGES: MdConfig = {
  table: "villages",
  title: "Villages",
  path: "/pos/villages",
  crudPath: "/pos/villages",
  softDelete: false,
  readOnly: true,
  createOnly: false,
  fields: [
    { name: "name_kh", label: "Khmer Name", display: true, required: true },
    { name: "name_en", label: "English Name", display: true, required: true },
  ],
  children: [],
  write: null,
};

// geo.c serve_pos_geo_workspace: the unified v_geo_hierarchy view —
// read_only + soft_delete (the view exposes deleted_at/created_at).
export const MD_GEO: MdConfig = {
  table: "v_geo_hierarchy",
  title: "Geo Locations",
  path: "/pos/geo",
  crudPath: "/pos/villages",
  softDelete: true,
  readOnly: true,
  createOnly: false,
  fields: [
    { name: "province_name", label: "Province", display: true, required: true },
    { name: "district_name", label: "District", display: true, required: true },
    { name: "commune_name", label: "Commune", display: true, required: true },
    { name: "village_name", label: "Village", display: true, required: true },
    { name: "village_code", label: "Code", display: true, required: true },
  ],
  children: [],
  write: null,
};

// pos.c md_dictionaries_columns + dict_fields { category, code, label } +
// opt { sort_order } — fields are already display columns (the one geo
// config whose webc form could post without 400ing).
export const MD_DICTIONARIES: MdConfig = {
  table: "dictionaries",
  title: "Dictionaries",
  path: "/pos/dictionaries",
  crudPath: "/pos/dictionaries",
  softDelete: false,
  readOnly: false,
  createOnly: true,
  fields: [
    { name: "category", label: "Category", display: true, required: true },
    { name: "code", label: "Code", display: true, required: true },
    { name: "label", label: "Label", display: true, required: true },
    { name: "sort_order", label: "Sort", display: true, num: true, required: true },
  ],
  children: [],
  write: {
    singular: "pos_dict",
    fallback: "/pos_dict",
    required: ["category", "code", "label"],
    optional: ["sort_order"],
    cols: ["category", "code", "label", "sort_order"],
    numKeys: ["sort_order"],
    softDelete: false,
  },
};

export const MD_CONFIGS: Record<string, MdConfig> = {
  provinces: MD_PROVINCES,
  districts: MD_DISTRICTS,
  communes: MD_COMMUNES,
  villages: MD_VILLAGES,
  geo: MD_GEO,
  dictionaries: MD_DICTIONARIES,
};

// ---- small helpers ------------------------------------------------------

// Display columns in thead/cell order.
export function mdDisplayCols(config: MdConfig): MdField[] {
  return config.fields.filter((f) => f.display === true);
}

// Row SELECT list: display columns first, then the form-only fields (the
// edit rows need their stored values to prefill).
function mdSelectCols(config: MdConfig): string[] {
  const cols: string[] = [];
  for (const f of config.fields) {
    if (!cols.includes(f.name)) cols.push(f.name);
  }
  return cols;
}

function cellValue(row: Record<string, unknown>): string {
  const v = row[Object.keys(row)[0] as never];
  return v === null || v === undefined ? "" : String(v);
}

// ---- search (md_search_add / md_where / md_bind_search) ------------------

const MD_MAX_SEARCH_TERMS = 8;
const MD_MAX_SEARCH_TOKENS = 8;

// One ANDed term: the box value split on spaces (every token must match),
// capped at 100 chars / 8 tokens — webc md_search_add exactly.
function addTerm(terms: MdSearchTerm[], col: string | null, raw: string | null): void {
  if (raw === null || raw.length === 0 || terms.length >= MD_MAX_SEARCH_TERMS) return;
  const pattern = raw.length > 100 ? raw.slice(0, 100) : raw;
  const tokens: string[] = [];
  for (const tok of pattern.split(" ")) {
    if (tok.length === 0) continue;
    tokens.push(tok);
    if (tokens.length >= MD_MAX_SEARCH_TERMS * MD_MAX_SEARCH_TOKENS) break;
  }
  if (tokens.length > 0) terms.push({ col, tokens });
}

// serve_master_child's search build: the legacy ?q + ?qcol pair first
// (col = NULL when the scope is unknown), then one term per filled
// ?q_<col> box over its own column.
export function mdBuildSearch(q: Query, config: MdConfig): MdSearchTerm[] {
  const terms: MdSearchTerm[] = [];
  const display = mdDisplayCols(config);
  const legacy = firstOf(q, "q");
  if (legacy !== null && legacy.length > 0) {
    const qcol = firstOf(q, "qcol") ?? "";
    const scoped = display.find((c) => c.name === qcol) ?? null;
    addTerm(terms, scoped ? scoped.name : null, legacy);
  }
  for (const col of display) {
    addTerm(terms, col.name, firstOf(q, `q_${col.name}`));
  }
  return terms;
}

export function mdSearchOn(terms: MdSearchTerm[]): boolean {
  return terms.length > 0;
}

// md_where: soft-delete scope + one parenthesized cell group PER TOKEN
// (LIKE over the scoped column, or every display column), groups ANDed.
// Returns the WHERE text plus the LIKE params in bind order (token-outer,
// column-inner — md_bind_search's loop).
function mdWhere(
  config: MdConfig,
  showDeleted: boolean,
  terms: MdSearchTerm[],
): { where: string; params: string[] } {
  let where = "";
  let joiner = " WHERE ";
  if (config.softDelete) {
    where = showDeleted ? " WHERE deleted_at IS NOT NULL" : " WHERE deleted_at IS NULL";
    joiner = " AND ";
  }
  const params: string[] = [];
  const display = mdDisplayCols(config).map((c) => c.name);
  for (const term of terms) {
    const cols = term.col !== null ? [term.col] : display;
    const cond = cols.map((c) => `CAST(${c} AS TEXT) LIKE ?`).join(" OR ");
    for (const tok of term.tokens) {
      where += `${joiner}(${cond})`;
      joiner = " AND ";
      for (const c of cols) {
        params.push(`%${tok}%`);
      }
    }
  }
  return { where, params };
}

// md_count_masters. A NULL search (the trash badge) ignores the filter.
export function mdCount(config: MdConfig, showDeleted: boolean, terms: MdSearchTerm[]): number {
  const { where, params } = mdWhere(config, showDeleted, terms);
  const row = queryOne<{ c: number }>(
    `SELECT count(*) AS c FROM ${config.table}${where};`,
    ...params,
  );
  return Number(row?.c ?? 0);
}

// ---- master loading -----------------------------------------------------

// md_load_master_with_children without the child window (lazy in the
// port): SELECT id + columns ... ORDER BY created_at DESC LIMIT/OFFSET.
export function mdLoadMasters(
  config: MdConfig,
  info: PageInfo,
  showDeleted: boolean,
  terms: MdSearchTerm[],
): MdRow[] {
  const { where, params } = mdWhere(config, showDeleted, terms);
  const cols = mdSelectCols(config);
  const rows = queryAll<Record<string, unknown>>(
    `SELECT id, ${cols.join(", ")} FROM ${config.table}${where} ` +
      `ORDER BY created_at DESC LIMIT ? OFFSET ?;`,
    ...params,
    info.perPage,
    info.offset,
  );
  return rows.map((r) => {
    const data: Record<string, string> = {};
    for (const c of cols) {
      const v = r[c];
      data[c] = v === null || v === undefined ? "" : String(v);
    }
    return { id: String(r.id ?? ""), data };
  });
}

// page_info_parse_session for the master window (query ?per_page= first,
// then the webc_per_page cookie) + the real total.
export function mdMasterInfo(
  q: Query,
  cookie: string | undefined,
  config: MdConfig,
  showDeleted: boolean,
  terms: MdSearchTerm[],
): PageInfo {
  const info = pageInfoParseSession(q, cookie, "page");
  pageInfoFinish(info, mdCount(config, showDeleted, terms));
  return info;
}

// ---- child loading (the lazy tab fragment) ------------------------------

// md_child_where: the fk condition (+ subquery), then the soft-delete
// filter — which md_child_where drops entirely when the child has no
// soft_delete (all three geo children), so deleted rows stay visible.
function childWhere(child: MdChild, showDeleted: boolean): string {
  const fk =
    child.fkCondition !== null
      ? child.fkCondition
      : `${child.fkColumn} = ?`;
  if (!child.softDelete) return fk;
  return `${fk} AND deleted_at ${showDeleted ? "IS NOT NULL" : "IS NULL"}`;
}

// The child's own write config (MD_CONFIGS[child.table]) — null means the
// child has no registered write routes (villages) and its tab renders no
// code input / FK select.
export function mdChildWrite(child: MdChild): MdWrite | null {
  return MD_CONFIGS[child.table]?.write ?? null;
}

// Child windows key off ?<table>_page / ?<table>_per_page only — webc
// page_info_parse (no session cookie), default 20.
export function childInfo(q: Query, child: MdChild): PageInfo {
  return pageInfoParse(q, `${child.table}_page`, `${child.table}_per_page`);
}

export function childCount(child: MdChild, masterId: string, showDeleted: boolean): number {
  const row = queryOne<{ c: number }>(
    `SELECT count(*) AS c FROM ${child.table} WHERE ${childWhere(child, showDeleted)};`,
    masterId,
  );
  return Number(row?.c ?? 0);
}

// md_child rows: id + display columns + the FK column (edit-row prefill)
// + the form-only code column, windowed by the child page, ORDER BY id
// DESC (load_child_rows).
export function loadChildRows(
  child: MdChild,
  masterId: string,
  info: PageInfo,
  showDeleted: boolean,
): MdRow[] {
  const cols: string[] = [];
  for (const c of child.columns) {
    if (!cols.includes(c.name)) cols.push(c.name);
  }
  if (!cols.includes(child.fkColumn)) cols.push(child.fkColumn);
  if (child.codeField && !cols.includes(child.codeField.name)) {
    cols.push(child.codeField.name);
  }
  const rows = queryAll<Record<string, unknown>>(
    `SELECT id, ${cols.join(", ")} FROM ${child.table} ` +
      `WHERE ${childWhere(child, showDeleted)} ` +
      `ORDER BY id DESC LIMIT ? OFFSET ?;`,
    masterId,
    info.perPage,
    info.offset,
  );
  return rows.map((r) => {
    const data: Record<string, string> = {};
    for (const c of cols) {
      const v = r[c];
      data[c] = v === null || v === undefined ? "" : String(v);
    }
    return { id: String(r.id ?? ""), data };
  });
}

// ---- option lists -------------------------------------------------------

export type MdOption = { id: string; label: string };

function loadOptions(sql: string, ...params: string[]): MdOption[] {
  return queryAll<Record<string, unknown>>(sql, ...params).map((r) => ({
    id: r.id === null || r.id === undefined ? "" : String(r.id),
    label: cellValue(r),
  }));
}

// Master-form FK selects: the whole live parent list (webc
// md_fk_options_where: deleted_at IS NULL, ordered by label).
export function masterFkOptions(field: MdField): MdOption[] {
  if (!field.fk) return [];
  return loadOptions(field.fk.optionsSql);
}

// The scoped child add/edit-row select: parents valid for THIS master row.
export function childFkOptions(child: MdChild, masterId: string): MdOption[] {
  return loadOptions(child.fkOptionsSql, masterId);
}

// md_fk_display's ungated fallback: a stored parent outside the live/
// scoped option list still renders its label, not a raw id.
export function fkFallbackLabel(field: MdField | undefined, id: string): string {
  if (!field?.fk || id.length === 0) return id;
  const row = queryOne<Record<string, unknown>>(
    `SELECT ${field.fk.labelCol} AS label FROM ${field.fk.table} WHERE id = ?;`,
    id,
  );
  const v = row?.label;
  return v === null || v === undefined || String(v).length === 0 ? id : String(v);
}

export function childFkFallbackLabel(child: MdChild, id: string): string {
  if (id.length === 0) return id;
  const row = queryOne<Record<string, unknown>>(
    `SELECT ${child.fkLabelCol} AS label FROM ${child.fkTable} WHERE id = ?;`,
    id,
  );
  const v = row?.label;
  return v === null || v === undefined || String(v).length === 0 ? id : String(v);
}

// ---- write handlers (SERVE_CREATE / UPDATE / SOFT_DELETE / RESTORE) -----

// Generated from the field list so the SQL matches geo.c/pos.c byte for
// byte: INSERT binds the cols in write order; UPDATE COALESCE(NULLIF(?,
// ''), col)s each one (empty keeps the stored value) — webc's update SQL.
type WriteTarget = { table: string; write: MdWrite };

function buildInsertSql(target: WriteTarget): string {
  const { table, write } = target;
  return `INSERT INTO ${table} (${write.cols.join(", ")}) VALUES (${write.cols
    .map(() => "?")
    .join(", ")});`;
}

function buildUpdateSql(target: WriteTarget): string {
  const { table, write } = target;
  const sets = write.cols.map((c) => `${c} = COALESCE(NULLIF(?, ''), ${c})`).join(", ");
  return `UPDATE ${table} SET ${sets} WHERE id = ?;`;
}

// SERVE_EXTRACT_FIELDS + SERVE_EXTRACT_OPT_FIELDS: required keys come
// from the body only (missing key or empty values[0] -> 400); optional
// keys fall body -> query -> "" (a present-but-empty body value must not
// shadow the query string — child forms carry the FK in the body).
function extractFields(
  form: FormData,
  searchParams: URLSearchParams,
  write: MdWrite,
): Record<string, string> | Response {
  const vals: Record<string, string> = {};
  for (const key of write.required) {
    const v = formText(form, key);
    if (v === null) return errorPage(400, "400 Bad Request");
    vals[key] = v;
  }
  if (write.required.length > 0 && vals[write.required[0]].length === 0) {
    return errorPage(400, "400 Bad Request");
  }
  for (const key of write.optional) {
    let v = formText(form, key);
    if (v === null || v.length === 0) v = searchParams.get(key) ?? "";
    vals[key] = v;
  }
  return vals;
}

function writeOutcome(msg: string | null, write: MdWrite): Response | null {
  if (msg === null) return null;
  if (isUniqueMsg(msg)) return uniquePage(write.singular, msg);
  return errorPage(500, "500 Internal Server Error");
}

type RouteCtx = { params: Promise<{ id: string }> };

// SERVE_CREATE(plural, singular, fields, opt_fields).
export function mdCreateHandler(target: WriteTarget) {
  const insert = buildInsertSql(target);
  const { write } = target;
  return async function POST(request: NextRequest): Promise<Response> {
    const form = await request.formData();
    const vals = extractFields(form, request.nextUrl.searchParams, write);
    if (vals instanceof Response) return vals;
    const binds = write.cols.map((c) =>
      write.numKeys.includes(c) && vals[c].length === 0 ? 0 : vals[c],
    );
    const msg = dbRun(() => {
      db.prepare(insert).run(...binds);
    });
    const err = writeOutcome(msg, write);
    if (err) return err;
    return redirect302(request, write.fallback);
  };
}

// SERVE_UPDATE: id gate -> extract -> COALESCE update -> 302.
export function mdUpdateHandler(target: WriteTarget) {
  const update = buildUpdateSql(target);
  const { write } = target;
  return async function POST(request: NextRequest, { params }: RouteCtx): Promise<Response> {
    const { id } = await params;
    if (!idValid(id)) return errorPage(404, "404 Not Found");
    const form = await request.formData();
    const vals = extractFields(form, request.nextUrl.searchParams, write);
    if (vals instanceof Response) return vals;
    const binds = write.cols.map((c) => vals[c]);
    const msg = dbRun(() => {
      db.prepare(update).run(...binds, id);
    });
    const err = writeOutcome(msg, write);
    if (err) return err;
    return redirect302(request, write.fallback);
  };
}

// SERVE_SOFT_DELETE + SERVE_SOFT_DELETE's restore half.
export function mdDeleteHandler(target: WriteTarget) {
  const { table, write } = target;
  const sql = `UPDATE ${table} SET deleted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?;`;
  return async function POST(request: NextRequest, { params }: RouteCtx): Promise<Response> {
    const { id } = await params;
    if (!idValid(id)) return errorPage(404, "404 Not Found");
    const msg = dbRun(() => {
      db.prepare(sql).run(id);
    });
    if (msg !== null) return errorPage(500, "500 Internal Server Error");
    return redirect302(request, write.fallback);
  };
}

export function mdRestoreHandler(target: WriteTarget) {
  const { table, write } = target;
  const sql = `UPDATE ${table} SET deleted_at = NULL WHERE id = ?;`;
  return async function POST(request: NextRequest, { params }: RouteCtx): Promise<Response> {
    const { id } = await params;
    if (!idValid(id)) return errorPage(404, "404 Not Found");
    const msg = dbRun(() => {
      db.prepare(sql).run(id);
    });
    if (msg !== null) return errorPage(500, "500 Internal Server Error");
    return redirect302(request, write.fallback);
  };
}

// GET on a write route: webc's route table registers POST only, so the
// method gate answers 404 before any handler runs.
export function mdGet404(): Response {
  return errorPage(404, "404 Not Found");
}
