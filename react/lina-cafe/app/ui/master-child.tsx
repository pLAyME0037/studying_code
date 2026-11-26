import { cookies, headers } from "next/headers";
import { AdminChrome } from "./admin-chrome";
import { Pagination } from "./pagination";
import { getI18n } from "@/app/lib/i18n";
import { idValid } from "@/app/lib/crud";
import {
  type MdChild,
  type MdConfig,
  type MdField,
  type MdOption,
  type MdRow,
  type MdSearchTerm,
  childCount,
  childFkFallbackLabel,
  childFkOptions,
  childInfo,
  fkFallbackLabel,
  loadChildRows,
  masterFkOptions,
  mdBuildSearch,
  mdChildWrite,
  mdCount,
  mdDisplayCols,
  mdLoadMasters,
  mdMasterInfo,
  mdSearchOn,
} from "@/app/lib/master-child";
import {
  PER_COOKIE,
  type PageInfo,
  type Query,
  firstOf,
  pageFragmentRequested,
  pageInfoFinish,
} from "@/app/lib/paging";

// display/component/master_child.h.tt + md_child_{tab,rows,add_row,
// query_row}.h.tt — the master/detail list page. Every class string below
// is copied verbatim from those templates; the structure differs only
// where slice 6 was approved to differ (see PLAN.md):
//   * forms gain the id-code + FK fields webc's write handlers require
//     (webc's geo4 create/update always answers 400);
//   * child tabs render as empty shells and load on first open via
//     ?child=<table>&master=<id>&fragment=1 (webc renders every child
//     window eagerly for every master row).
//
// Three response shapes from one component:
//   1. full page        -> admin chrome + #mc-root (master_child.h.tt);
//   2. master fragment  -> #pg-frag + tbody#mc-tbody + nav (PaginationSwitcher);
//   3. child fragment   -> one .tab-content (mdTabLoad's lazy fetch).

type Tr = (key: string, fallback: string) => string;

// master_child.h.tt's inline 400 ms query debounce, copied verbatim at the
// same position in the document. In webc_pos this script is emitted BEFORE
// the query-row inputs it binds (verified live: script ends byte 37625,
// first data-md-qbox at 41327), so boxes.length === 0 and it never binds —
// the port keeps it exactly where webc has it (markup parity) and records
// the finding as a webc bug rather than silently "fixing" the position.
const MD_QUERY_DEBOUNCE = `
    (function () {
        var boxes = document.querySelectorAll('[data-md-qbox]');
        if (!boxes.length) return;
        var f = document.getElementById('md-query-form');
        var timer;
        function kick() {
            clearTimeout(timer);
            timer = setTimeout(function () { f.submit(); }, 400);
        }
        for (var i = 0; i < boxes.length; ++i) {
            boxes[i].addEventListener('input', kick);
            boxes[i].addEventListener('change', kick);
        }
        for (var j = 0; j < boxes.length; ++j) {
            if (boxes[j].value) {
                boxes[j].focus();
                if (boxes[j].tagName === 'INPUT') {
                    boxes[j].setSelectionRange(boxes[j].value.length,
                                               boxes[j].value.length);
                }
                break;
            }
        }
    })();
`;

const INPUT =
  "w-full px-0.5 py-1.5 border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white";
const INPUT_SM =
  "w-full px-0.5 py-1.5 text-sm border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white";
const INPUT_SM_FLEX =
  "px-0.5 py-1.5 text-sm border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white";

// ---- icons (verbatim path data from the templates) ----------------------

function IconEdit() {
  return (
    <svg className="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
      <path
        strokeLinecap="round"
        strokeLinejoin="round"
        d="M11 5H6a2 2 0 00-2 2v11a2 2 0 002 2h11a2 2 0 002-2v-5m-1.414-9.414a2 2 0 112.828 2.828L11.828 15H9v-2.828l8.586-8.586z"
      />
    </svg>
  );
}

function IconTrash() {
  return (
    <svg className="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
      <path
        strokeLinecap="round"
        strokeLinejoin="round"
        d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"
      />
    </svg>
  );
}

function IconRestore() {
  return (
    <svg className="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
      <path
        strokeLinecap="round"
        strokeLinejoin="round"
        d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15"
      />
    </svg>
  );
}

function IconCheck() {
  return (
    <svg className="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
      <path strokeLinecap="round" strokeLinejoin="round" d="M5 13l4 4L19 7" />
    </svg>
  );
}

function IconX() {
  return (
    <svg className="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
      <path strokeLinecap="round" strokeLinejoin="round" d="M6 18L18 6M6 6l12 12" />
    </svg>
  );
}

function IconSearch() {
  return (
    <svg className="w-6 h-6" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
      <path
        strokeLinecap="round"
        strokeLinejoin="round"
        d="M21 21l-4.35-4.35M17 11a6 6 0 11-12 0 6 6 0 0112 0z"
      />
    </svg>
  );
}

function IconPlus() {
  return (
    <svg className="w-4 h-4" fill="none" stroke="currentColor" strokeWidth="2" viewBox="0 0 24 24">
      <path strokeLinecap="round" strokeLinejoin="round" d="M12 4v16m8-8H4" />
    </svg>
  );
}

// master_child.h.tt's expand button (the JS swaps it for its own 20x20
// eye-close glyph once opened — ListExpandSwitcher.js keeps those).
function IconEye() {
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      width="24"
      height="24"
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
    >
      <path d="M2 12s3-7 10-7 10 7 10 7-3 7-10 7-10-7-10-7Z" />
      <circle cx="12" cy="12" r="3" />
    </svg>
  );
}

// ---- field controls -----------------------------------------------------

// One form input (create form, master edit row, child add/edit rows).
// `value` undefined = empty (create/add); prefilled rows use defaultValue
// so React never flags a controlled input without onChange.
function MdFieldControl({
  field,
  value,
  formId,
  options,
  className,
}: {
  field: MdField;
  value?: string;
  formId?: string;
  options?: MdOption[];
  className: string;
}) {
  if (field.fk) {
    return (
      <FkSelect
        name={field.name}
        options={options ?? []}
        required={field.required}
        value={value}
        formId={formId}
        ariaLabel={field.label}
        className={className}
        fallbackLabel={(id) => fkFallbackLabel(field, id)}
      />
    );
  }
  if (field.num) {
    return (
      <input
        name={field.name}
        form={formId}
        type="number"
        step="any"
        required={field.required}
        defaultValue={value}
        className={className}
      />
    );
  }
  return (
    <input
      name={field.name}
      form={formId}
      type="text"
      required={field.required}
      defaultValue={value}
      className={className}
    />
  );
}

// webc's COL_TYPE_FK_SELECT option list: the live parents, plus a selected
// escape option when the stored id is outside the list (md_opt_has_id's
// fallback — a soft-deleted parent keeps its label instead of a raw id).
function FkSelect({
  name,
  options,
  required,
  value,
  formId,
  ariaLabel,
  className,
  fallbackLabel,
}: {
  name: string;
  options: MdOption[];
  required?: boolean;
  value?: string;
  formId?: string;
  ariaLabel?: string;
  className: string;
  fallbackLabel?: (id: string) => string;
}) {
  const outside =
    value !== undefined &&
    value !== "" &&
    !options.some((o) => o.id === value) &&
    fallbackLabel !== undefined;
  return (
    <select
      name={name}
      form={formId}
      required={required}
      defaultValue={value}
      aria-label={ariaLabel}
      className={className}
    >
      {options.map((o) => (
        <option key={o.id} value={o.id}>
          {o.label}
        </option>
      ))}
      {outside && <option value={value}>{fallbackLabel(value)}</option>}
    </select>
  );
}

// ---- search row ----------------------------------------------------------

type QueryState = {
  terms: MdSearchTerm[];
  qText: string; // legacy ?q, capped at 100 (serve_master_child)
  scopedCol: string; // matching ?qcol, else "" (no legacy scope)
};

function buildQueryState(config: MdConfig, sp: Query): QueryState {
  const terms = mdBuildSearch(sp, config);
  const raw = firstOf(sp, "q") ?? "";
  const qText = raw.length > 100 ? raw.slice(0, 100) : raw;
  const qcol = firstOf(sp, "qcol") ?? "";
  const scoped =
    raw.length > 0 && mdDisplayCols(config).some((c) => c.name === qcol)
      ? qcol
      : "";
  return { terms, qText, scopedCol: scoped };
}

// md_child_query_row.h.tt: one search box per display column posting into
// the empty #md-query-form anchor, plus the submit + clear buttons.
function QueryRow({
  config,
  sp,
  qText,
  scopedCol,
  searchActive,
  tr,
}: {
  config: MdConfig;
  sp: Query;
  qText: string;
  scopedCol: string;
  searchActive: boolean;
  tr: Tr;
}) {
  return (
    <tr className="border-b border-gray-200 dark:border-gray-700 bg-gray-50 dark:bg-gray-900">
      <td className="pl-3 py-1">0</td>
      {mdDisplayCols(config).map((col) => {
        const box = firstOf(sp, `q_${col.name}`);
        const shown =
          box !== null && box.length > 0
            ? box
            : scopedCol === col.name
              ? qText
              : "";
        return (
          <td key={col.name} className="pl-3 py-1">
            <input
              type="search"
              name={`q_${col.name}`}
              form="md-query-form"
              data-md-qbox=""
              defaultValue={shown}
              placeholder={tr(col.label, col.label)}
              aria-label={`${tr("Search", "Search")} ${tr(col.label, col.label)}`}
              className="w-full px-1 py-1.5 text-sm border border-gray-300 dark:border-gray-600 bg-white dark:bg-gray-800 text-gray-800 dark:text-white"
            />
          </td>
        );
      })}
      <td className="px-3">
        <div className="flex items-center gap-1 justify-end">
          <button
            type="submit"
            form="md-query-form"
            title={tr("Search", "Search")}
            aria-label={tr("Search", "Search")}
            className="shrink-0 text-gray-500 hover:text-gray-900 dark:hover:text-white"
          >
            <IconSearch />
          </button>
          {searchActive && (
            <a
              href={config.path}
              title="Clear the search"
              className="shrink-0 px-1 text-lg text-gray-400 hover:text-gray-700 dark:hover:text-gray-200"
            >
              &#10005;
            </a>
          )}
        </div>
      </td>
    </tr>
  );
}

// ---- master rows ---------------------------------------------------------

function loadFkOptions(config: MdConfig): Map<string, MdOption[]> {
  const map = new Map<string, MdOption[]>();
  for (const f of config.fields) {
    if (f.fk) map.set(f.name, masterFkOptions(f));
  }
  return map;
}

function MasterRow({
  config,
  row,
  index,
  showDeleted,
  tr,
  fkOptions,
}: {
  config: MdConfig;
  row: MdRow;
  index: number;
  showDeleted: boolean;
  tr: Tr;
  fkOptions: Map<string, MdOption[]>;
}) {
  const display = mdDisplayCols(config);
  const colSpan = display.length + 2;
  const editable = !config.readOnly && !config.createOnly;
  const editFormId = `md-m-${config.table}-${row.id}`;
  const rowCls = showDeleted
    ? "hover:bg-red-100 dark:hover:bg-red-800 bg-red-50 dark:bg-red-900"
    : "hover:bg-gray-100 dark:hover:bg-gray-700";

  return (
    <>
      <tr className={rowCls} data-row-id={row.id}>
        <td className="pl-3 py-1 text-gray-700 dark:text-gray-200">{index}</td>
        {display.map((c) => (
          <td key={c.name} className="pl-3 py-1 text-gray-700 dark:text-gray-200">
            {row.data[c.name]}
          </td>
        ))}
        <td className="pl-3 py-1 whitespace-nowrap">
          <div className="flex items-center gap-1">
            {editable && config.softDelete && showDeleted ? (
              <form
                method="POST"
                action={`${config.crudPath}/${row.id}/restore?redirect=${config.path}?deleted=1`}
                data-md-kind="master"
                data-md-scope={config.table}
                data-md-op="restore"
                data-md-master={row.id}
                data-md-target={row.id}
                className="inline"
              >
                <button
                  type="submit"
                  title={tr("Restore", "Restore")}
                  aria-label={tr("Restore", "Restore")}
                  className="p-1 text-green-600 hover:bg-green-100 dark:hover:bg-green-900/40"
                >
                  <IconRestore />
                </button>
              </form>
            ) : (
              <>
                {editable && (
                  <button
                    type="button"
                    className="master-edit-btn p-1 text-amber-600 hover:bg-amber-100 dark:hover:bg-amber-900/30"
                    title={tr("Edit", "Edit")}
                    aria-label={tr("Edit", "Edit")}
                    data-master-id={row.id}
                  >
                    <IconEdit />
                  </button>
                )}
                {config.children.length > 0 && (
                  <button
                    type="button"
                    className="expand-btn p-1 text-sm hover:bg-indigo-900/30 text-indigo-600"
                    data-master-id={row.id}
                    aria-expanded="false"
                  >
                    <IconEye />
                  </button>
                )}
                {editable && (
                  <form
                    method="POST"
                    action={`${config.crudPath}/${row.id}/delete?redirect=${config.path}`}
                    data-md-kind="master"
                    data-md-scope={config.table}
                    data-md-op="delete"
                    data-md-master={row.id}
                    data-md-target={row.id}
                    className="inline"
                  >
                    <button
                      type="submit"
                      title={tr("Delete", "Delete")}
                      aria-label={tr("Delete", "Delete")}
                      className="p-1 text-red-600 hover:bg-red-100 dark:hover:bg-red-900/30"
                    >
                      <IconTrash />
                    </button>
                  </form>
                )}
              </>
            )}
          </div>
        </td>
      </tr>

      {editable && (
        <tr className="md-master-edit-row hidden">
          <td colSpan={colSpan} className="pl-3 py-1 bg-amber-50 dark:bg-gray-900">
            <form
              id={editFormId}
              method="POST"
              action={`${config.crudPath}/${row.id}/update?redirect=${config.path}`}
              data-md-kind="master"
              data-md-scope={config.table}
              data-md-op="update"
              data-md-master={row.id}
              data-md-target={row.id}
              className="flex flex-wrap gap-3 items-end"
            >
              {config.fields.map((f) => (
                <div key={f.name} className="flex flex-col gap-1 min-w-35">
                  <label className="text-sm text-gray-500 dark:text-gray-400">
                    {f.label}
                  </label>
                  <MdFieldControl
                    field={f}
                    value={row.data[f.name] ?? ""}
                    formId={editFormId}
                    options={fkOptions.get(f.name)}
                    className={INPUT_SM_FLEX}
                  />
                </div>
              ))}
              <button
                type="submit"
                title="Confirm"
                aria-label="Confirm"
                className="p-1 text-green-600 hover:bg-green-100 dark:hover:bg-green-900/40"
              >
                <IconCheck />
              </button>
              <button
                type="button"
                className="md-discard p-1 text-gray-500 hover:bg-gray-200 dark:hover:bg-gray-700"
                title="Discard"
                aria-label="Discard"
              >
                <IconX />
              </button>
            </form>
          </td>
        </tr>
      )}

      {/* Rendered for EVERY master row (master_child.h.tt line 604) — only
          the expand button is children-gated, so a child-less page still
          carries the empty panel row. */}
      <tr className="detail-panel-row" data-master-id={row.id} style={{ display: "none" }}>
        <td colSpan={colSpan} className="px-0 py-0">
          <div className="detail-panel bg-gray-50 dark:bg-gray-900 border-t border-gray-200 dark:border-gray-700">
            <div className="flex border-b border-gray-200 dark:border-gray-700 flex-wrap">
              {config.children.map((c, ti) => (
                <button
                  key={c.table}
                  type="button"
                  className={
                    "tab-btn px-3 py-1.5 text-sm font-medium " +
                    (ti === 0
                      ? "text-indigo-600 border-b-2 border-indigo-600"
                      : "text-gray-500 hover:text-gray-700 dark:text-gray-400")
                  }
                  data-tab={c.table}
                >
                  {tr(c.title, c.title)}
                </button>
              ))}
            </div>
            {config.children.map((c, ti) => (
              <ChildTabShell
                key={c.table}
                child={c}
                masterId={row.id}
                first={ti === 0}
                canAdd={!config.readOnly && !showDeleted}
                tr={tr}
              />
            ))}
          </div>
        </td>
      </tr>
    </>
  );
}

// ---- child tabs ----------------------------------------------------------

function ChildTabHeader({
  child,
  masterId,
  canAdd,
  tr,
}: {
  child: MdChild;
  masterId: string;
  canAdd: boolean;
  tr: Tr;
}) {
  return (
    <div className="bg-slate-100 dark:bg-slate-900 border border-gray-200 dark:border-gray-700 flex justify-between items-center">
      <div className="flex items-center pl-3 gap-2">
        <h4 className="font-medium text-sm text-gray-800 dark:text-white">
          {tr(child.title, child.title)}
        </h4>
        <span className="text-xs px-2 py-0.5 bg-gray-100 dark:bg-gray-700 text-gray-500 dark:text-gray-300">
          {child.fkColumn}: {masterId}
        </span>
      </div>
      {canAdd && (
        <button
          type="button"
          title="Add"
          aria-label="Add"
          data-tab={child.table}
          className="add-child-btn flex items-center gap-1 p-1 text-xs text-indigo-600 hover:bg-indigo-50 dark:hover:bg-indigo-900/40"
        >
          <IconPlus />
        </button>
      )}
    </div>
  );
}

function ChildTabHead({ child, tr }: { child: MdChild; tr: Tr }) {
  return (
    <thead className="bg-slate-100 dark:bg-slate-900 dark:text-white text-slate-800">
      <tr className="border-b border-gray-200 dark:border-gray-700">
        {child.columns.map((c) => (
          <th
            key={c.name}
            className="px-3 py-1.5 font-medium text-xs uppercase tracking-wide text-gray-500 dark:text-gray-400"
          >
            {tr(c.label, c.label)}
          </th>
        ))}
        <th className="px-3 py-1.5 font-medium text-xs uppercase tracking-wide text-gray-500 dark:text-gray-400 w-20">
          Actions
        </th>
      </tr>
    </thead>
  );
}

// The pre-load shell: header + head + an empty id'd tbody. mdTabLoad()
// swaps the whole node for the fetched tab-content on first open.
function ChildTabShell({
  child,
  masterId,
  first,
  canAdd,
  tr,
}: {
  child: MdChild;
  masterId: string;
  first: boolean;
  canAdd: boolean;
  tr: Tr;
}) {
  return (
    <div
      className={first ? "tab-content" : "tab-content hidden"}
      data-tab={child.table}
      data-master-id={masterId}
    >
      <ChildTabHeader child={child} masterId={masterId} canAdd={canAdd} tr={tr} />
      <table className="w-full text-left border-collapse text-sm">
        <ChildTabHead child={child} tr={tr} />
        <tbody id={`mc-tbody-${child.table}-${masterId}`}></tbody>
      </table>
    </div>
  );
}

// The scoped parent select + the form-only id-code input, stacked above
// Confirm/Discard in the row's Actions cell (they post into the hidden
// form via the form= attribute). Both are the slice-6 fix for webc's
// wrong-parent query fk / missing required code key.
function ChildRowExtras({
  child,
  formId,
  value,
  fkOptions,
  className,
}: {
  child: MdChild;
  formId: string;
  value?: string;
  fkOptions: MdOption[];
  className: string;
}) {
  const childWrite = mdChildWrite(child);
  if (!childWrite || !child.codeField) return null;
  return (
    <>
      <input
        form={formId}
        name={child.codeField.name}
        type="text"
        required={child.codeField.required}
        defaultValue={value === undefined ? undefined : value}
        placeholder={child.codeField.label}
        aria-label={child.codeField.label}
        className={className}
      />
      <FkSelect
        name={child.fkColumn}
        options={fkOptions}
        required
        value={value === undefined ? undefined : value}
        formId={formId}
        ariaLabel={child.fkLabel}
        className={className}
        fallbackLabel={(id) => childFkFallbackLabel(child, id)}
      />
    </>
  );
}

// md_child_add_row.h.tt — hidden until the tab's + button is pressed.
function ChildAddRow({
  child,
  masterId,
  fkOptions,
  tr,
}: {
  child: MdChild;
  masterId: string;
  fkOptions: MdOption[];
  tr: Tr;
}) {
  const formId = `mdf-a-${child.table}-${masterId}`;
  return (
    <tr className="md-add-row hidden">
      {child.columns.map((c) => (
        <td key={c.name} className="p-1">
          <MdFieldControl field={c} formId={formId} className={INPUT_SM} />
        </td>
      ))}
      <td className="p-1">
        <div className="flex flex-col gap-1 items-start">
          <ChildRowExtras
            child={child}
            formId={formId}
            fkOptions={fkOptions}
            className={INPUT_SM}
          />
          <button
            type="submit"
            form={formId}
            title="Confirm"
            aria-label="Confirm"
            className="p-1 text-green-600 hover:bg-green-100 dark:hover:bg-green-900/40"
          >
            <IconCheck />
          </button>
          <button
            type="button"
            className="md-discard p-1 text-gray-500 hover:bg-gray-200 dark:hover:bg-gray-700"
            title="Discard"
            aria-label="Discard"
          >
            <IconX />
          </button>
        </div>
      </td>
    </tr>
  );
}

// md_child_rows.h.tt — the display row, its paired per-td edit row, and
// the hidden update form (kept inside the edit row's Actions cell so the
// markup stays valid HTML; webc emits it as a tbody sibling, which the
// parser may re-parent — ListExpandSwitcher's tr.querySelector('form')
// discard contract still holds either way).
function ChildRowPair({
  config,
  child,
  masterId,
  row,
  showDeleted,
  fkOptions,
}: {
  config: MdConfig;
  child: MdChild;
  masterId: string;
  row: MdRow;
  showDeleted: boolean;
  fkOptions: MdOption[];
}) {
  const formId = `mdf-e-${child.table}-${row.id}`;
  const rowCls = showDeleted
    ? "md-row hover:bg-red-100 dark:hover:bg-red-800 bg-red-50 dark:bg-red-900"
    : "md-row hover:bg-gray-100 dark:hover:bg-gray-700";
  return (
    <>
      <tr className={rowCls} data-row-id={row.id}>
        {child.columns.map((c) => (
          <td key={c.name} className="pl-3 py-1 text-sm text-gray-700 dark:text-gray-200">
            {row.data[c.name]}
          </td>
        ))}
        <td className="p-1 whitespace-nowrap">
          <div className="flex gap-1 items-center">
            {child.softDelete && showDeleted ? (
              <form
                method="POST"
                action={`${child.crudPath}/${row.id}/restore?redirect=${config.path}?deleted=1`}
                data-md-kind="child"
                data-md-scope={child.table}
                data-md-op="restore"
                data-md-master={masterId}
                data-md-target={row.id}
                className="inline"
              >
                <button
                  type="submit"
                  title="Restore"
                  aria-label="Restore"
                  className="p-1 text-green-600 hover:bg-green-100 dark:hover:bg-green-900/40"
                >
                  <IconRestore />
                </button>
              </form>
            ) : (
              <>
                {!config.readOnly && (
                  <button
                    type="button"
                    className="child-edit-btn p-1 text-amber-600 hover:bg-amber-100 dark:hover:bg-amber-900/40"
                    title="Edit"
                    aria-label="Edit"
                  >
                    <IconEdit />
                  </button>
                )}
                {!config.readOnly && (
                  <form
                    method="POST"
                    action={`${child.crudPath}/${row.id}/delete?redirect=${config.path}`}
                    data-md-kind="child"
                    data-md-scope={child.table}
                    data-md-op="delete"
                    data-md-master={masterId}
                    data-md-target={row.id}
                    className="inline"
                  >
                    <button
                      type="submit"
                      title="Delete"
                      aria-label="Delete"
                      className="p-1 text-red-600 hover:bg-red-100 dark:hover:bg-red-900/30"
                    >
                      <IconTrash />
                    </button>
                  </form>
                )}
              </>
            )}
          </div>
        </td>
      </tr>

      <tr className="md-edit-row hidden">
        {child.columns.map((c) => (
          <td key={c.name} className="p-1">
            <MdFieldControl
              field={c}
              value={row.data[c.name]}
              formId={formId}
              className={INPUT_SM}
            />
          </td>
        ))}
        <td className="p-1">
          <div className="flex flex-col gap-1 items-start">
            <ChildRowExtras
              child={child}
              formId={formId}
              value={child.codeField ? row.data[child.codeField.name] : undefined}
              fkOptions={fkOptions}
              className={INPUT_SM}
            />
            <button
              type="submit"
              form={formId}
              title="Confirm"
              aria-label="Confirm"
              className="p-1 text-green-600 hover:bg-green-100 dark:hover:bg-green-900/40"
            >
              <IconCheck />
            </button>
            <button
              type="button"
              className="md-discard p-1 text-gray-500 hover:bg-gray-200 dark:hover:bg-gray-700"
              title="Discard"
              aria-label="Discard"
            >
              <IconX />
            </button>
          </div>
        </td>
      </tr>

      <form
        id={formId}
        method="POST"
        action={`${child.crudPath}/${row.id}/update?redirect=${config.path}`}
        encType="multipart/form-data"
        data-md-kind="child"
        data-md-scope={child.table}
        data-md-op="update"
        data-md-master={masterId}
        data-md-target={row.id}
        className="hidden"
      />
    </>
  );
}

// md_child_tab.h.tt — the whole tab. Serves both the SSR lazy shell's
// loaded state and the ?child=&master=&fragment=1 response mdTabLoad()
// inserts (that response always arrives active, so no hidden class).
function ChildTab({
  config,
  child,
  masterId,
  sp,
  showDeleted,
  rawSearch,
  tr,
}: {
  config: MdConfig;
  child: MdChild;
  masterId: string;
  sp: Query;
  showDeleted: boolean;
  rawSearch: string;
  tr: Tr;
}) {
  const info = childInfo(sp, child);
  pageInfoFinish(info, childCount(child, masterId, showDeleted));
  const rows = loadChildRows(child, masterId, info, showDeleted);
  const childWrite = mdChildWrite(child);
  const fkOptions = childWrite ? childFkOptions(child, masterId) : [];
  const container = `mc-tbody-${child.table}-${masterId}`;
  const addFormId = `mdf-a-${child.table}-${masterId}`;

  return (
    <div className="tab-content" data-tab={child.table} data-master-id={masterId}>
      <ChildTabHeader
        child={child}
        masterId={masterId}
        canAdd={!config.readOnly && !showDeleted}
        tr={tr}
      />
      <table className="w-full text-left border-collapse text-sm">
        <ChildTabHead child={child} tr={tr} />
        <tbody id={container}>
          <ChildAddRow child={child} masterId={masterId} fkOptions={fkOptions} tr={tr} />
          {rows.length === 0 ? (
            <tr>
              <td
                colSpan={child.columns.length + 1}
                className="px-3 py-1.5 text-sm text-gray-400 text-center"
              >
                No records
              </td>
            </tr>
          ) : (
            rows.map((row) => (
              <ChildRowPair
                key={row.id}
                config={config}
                child={child}
                masterId={masterId}
                row={row}
                showDeleted={showDeleted}
                fkOptions={fkOptions}
              />
            ))
          )}
        </tbody>
      </table>

      <Pagination
        info={info}
        base={config.path}
        container={container}
        rawSearch={rawSearch}
      />

      {/* webc's action carries ?<fk>=<master>; the body FK select wins
          (SERVE_EXTRACT_OPT_FIELDS is body-first), so the query param is
          dropped — posting it is what sent the wrong parent. */}
      <form
        id={addFormId}
        method="POST"
        action={`${child.crudPath}/create?redirect=${config.path}`}
        encType="multipart/form-data"
        data-md-kind="child"
        data-md-scope={child.table}
        data-md-op="create"
        data-md-master={masterId}
        className="hidden"
      />

      {info.total > 0 && (
        <div className="px-3 py-1 text-xs text-gray-500 dark:text-gray-400">
          {info.total} record(s)
        </div>
      )}
    </div>
  );
}

// ---- page branches -------------------------------------------------------

async function MasterFragment({
  config,
  sp,
}: {
  config: MdConfig;
  sp: Query;
}) {
  const jar = await cookies();
  const rawSearch = (await headers()).get("x-search") ?? "";
  const showDeleted = config.softDelete && firstOf(sp, "deleted") === "1";
  const { terms } = buildQueryState(config, sp);
  const info = mdMasterInfo(
    sp,
    jar.get(PER_COOKIE)?.value,
    config,
    showDeleted,
    terms,
  );
  const rows = mdLoadMasters(config, info, showDeleted, terms);
  const { tr } = await getI18n();
  const fkOptions = loadFkOptions(config);
  const display = mdDisplayCols(config);

  return (
    <div id="pg-frag">
      <table data-pg-rows="mc-tbody">
        <tbody id="mc-tbody">
          {rows.length === 0 ? (
            <tr>
              <td
                colSpan={display.length + 2}
                className="pl-3 py-1.5 text-sm text-gray-500 dark:text-gray-400 text-center"
              >
                {tr("No", "No")} {tr(config.title, config.title)} {tr("yet.", "yet.")}
              </td>
            </tr>
          ) : (
            rows.map((row, ri) => (
              <MasterRow
                key={row.id}
                config={config}
                row={row}
                index={info.offset + ri + 1}
                showDeleted={showDeleted}
                tr={tr}
                fkOptions={fkOptions}
              />
            ))
          )}
        </tbody>
      </table>
      <Pagination info={info} base={config.path} container="mc-tbody" rawSearch={rawSearch} />
    </div>
  );
}

async function ChildTabFragment({
  config,
  child,
  masterId,
  sp,
}: {
  config: MdConfig;
  child: MdChild;
  masterId: string;
  sp: Query;
}) {
  const rawSearch = (await headers()).get("x-search") ?? "";
  const showDeleted = config.softDelete && firstOf(sp, "deleted") === "1";
  const { tr } = await getI18n();
  return (
    <ChildTab
      config={config}
      child={child}
      masterId={masterId}
      sp={sp}
      showDeleted={showDeleted}
      rawSearch={rawSearch}
      tr={tr}
    />
  );
}

async function MasterFullPage({
  config,
  sp,
}: {
  config: MdConfig;
  sp: Query;
}) {
  const jar = await cookies();
  const rawSearch = (await headers()).get("x-search") ?? "";
  const showDeleted = config.softDelete && firstOf(sp, "deleted") === "1";
  const { terms, qText, scopedCol } = buildQueryState(config, sp);
  const info = mdMasterInfo(
    sp,
    jar.get(PER_COOKIE)?.value,
    config,
    showDeleted,
    terms,
  );
  const trashTotal = config.softDelete ? mdCount(config, true, []) : 0;
  const rows = mdLoadMasters(config, info, showDeleted, terms);
  const { tr } = await getI18n();
  const fkOptions = loadFkOptions(config);
  const display = mdDisplayCols(config);
  const searchActive = mdSearchOn(terms);
  const editable = !config.readOnly && !config.createOnly;

  return (
    <AdminChrome title={config.title}>
      <div id="mc-root" className="max-w-full mx-auto">
        {config.softDelete && (
          <div className="mb-2 inline-flex text-sm border border-gray-200 dark:border-gray-700 divide-x divide-gray-200 dark:divide-gray-700">
            <a
              href={config.path}
              title="Plain list of live rows"
              className={
                "px-2 py-1.5 font-medium " +
                (showDeleted
                  ? "text-subtext0 hover:text-text hover:bg-surface0"
                  : "bg-blue text-onbase")
              }
            >
              Live
            </a>
            <a
              href={`${config.path}?deleted=1`}
              title="Soft-deleted rows - restore or leave them here"
              className={
                "px-2 py-1.5 font-medium " +
                (showDeleted
                  ? "bg-red text-onbase"
                  : "text-subtext0 hover:text-text hover:bg-surface0")
              }
            >
              Trash (<span data-trash-total>{trashTotal}</span>)
            </a>
          </div>
        )}

        <form id="md-query-form" method="GET" action={config.path} />
        <script dangerouslySetInnerHTML={{ __html: MD_QUERY_DEBOUNCE }} />

        {showDeleted && (
          <div className="mb-3 px-2 py-1 flex justify-between items-center text-sm bg-red-50 dark:bg-red-900 border border-red-200 dark:border-red-800 text-red-700 dark:text-red-300">
            <span>
              Deleted rows &mdash; restore to bring them back, or leave them here.
            </span>
            <a href={config.path} className="underline">
              Back to list
            </a>
          </div>
        )}

        {editable && !showDeleted && (
          <form
            method="POST"
            action={`${config.crudPath}/create?redirect=${config.path}`}
            encType="multipart/form-data"
            data-md-kind="master"
            data-md-scope={config.table}
            data-md-op="create"
            className="mb-3 p-2 bg-white dark:bg-gray-800 border border-gray-200 dark:border-gray-700"
          >
            <h3 className="text-sm font-semibold text-gray-700 dark:text-gray-200 mb-3">
              {tr("Add", "Add")} {tr(config.title, config.title)}
            </h3>
            <div className="grid grid-cols-1 md:grid-cols-4 gap-4">
              {config.fields.map((f) => (
                <div key={f.name}>
                  <label className="block text-sm text-gray-600 dark:text-gray-300 mb-1">
                    {tr(f.label, f.label)}
                  </label>
                  <MdFieldControl
                    field={f}
                    options={fkOptions.get(f.name)}
                    className={INPUT}
                  />
                </div>
              ))}
            </div>
            <div className="flex items-center gap-3 justify-end">
              <button
                type="submit"
                className="bg-gray-200 hover:bg-gray-200/30 dark:bg-gray-700 hover:dark:bg-gray-700/30 text-sky-900 hover:text-sky-950 dark:text-sky-200 hover:dark:text-sky-700 border-b-3 border-b-sky-200 hover:border-b-sky-700 mt-1 px-1 py-0.5 font-medium"
              >
                Confirm
              </button>
              <button
                type="reset"
                className="bg-gray-200 hover:bg-gray-200/30 dark:bg-gray-700 hover:dark:bg-gray-700/30 text-orange-900 hover:text-orange-950 dark:text-orange-200 hover:dark:text-orange-700 border-b-3 border-b-orange-200 hover:border-b-orange-700 mt-1 px-1 py-0.5"
              >
                Discard
              </button>
            </div>
          </form>
        )}

        <div className="bg-white dark:bg-gray-800 border-gray-200 dark:border-gray-700 overflow-scroll border">
          <table className="w-full text-left border-collapse master-table">
            <thead className="bg-slate-100 dark:bg-slate-900 dark:text-white text-slate-800">
              <tr className="border-b border-gray-200 dark:border-gray-700">
                <th className="py-1 pl-3 font-semibold">{"\u2116"}</th>
                {display.map((c) => (
                  <th key={c.name} className="py-1 pl-3 font-semibold">
                    {tr(c.label, c.label)}
                  </th>
                ))}
                {/* webc gates this header on having children too, so a
                    child-less master ships 4 th / 5 td — kept verbatim. */}
                {editable && config.children.length > 0 && (
                  <th className="py-1 pl-3 font-semibold">Actions</th>
                )}
              </tr>
              <QueryRow
                config={config}
                sp={sp}
                qText={qText}
                scopedCol={scopedCol}
                searchActive={searchActive}
                tr={tr}
              />
            </thead>
            <tbody id="mc-tbody" className="divide-y divide-gray-200 dark:divide-gray-700">
              {rows.length === 0 ? (
                <tr>
                  <td
                    colSpan={display.length + 2}
                    className="pl-3 py-1.5 text-sm text-gray-500 dark:text-gray-400 text-center"
                  >
                    {tr("No", "No")} {tr(config.title, config.title)} {tr("yet.", "yet.")}
                  </td>
                </tr>
              ) : (
                rows.map((row, ri) => (
                  <MasterRow
                    key={row.id}
                    config={config}
                    row={row}
                    index={info.offset + ri + 1}
                    showDeleted={showDeleted}
                    tr={tr}
                    fkOptions={fkOptions}
                  />
                ))
              )}
            </tbody>
          </table>
        </div>

        <Pagination info={info} base={config.path} container="mc-tbody" rawSearch={rawSearch} />
      </div>
    </AdminChrome>
  );
}

// One entry point for the six /pos pages: picks the response shape from
// the query (?fragment=1 + ?child= + ?master= -> one tab-content, else the
// master fragment, else the full page).
export async function MasterChildPage({
  config,
  searchParams,
}: {
  config: MdConfig;
  searchParams: Promise<Query>;
}) {
  const sp = await searchParams;

  if (pageFragmentRequested(sp)) {
    const childKey = firstOf(sp, "child");
    const masterId = firstOf(sp, "master");
    const child =
      childKey === null ? undefined : config.children.find((c) => c.table === childKey);
    if (child && masterId !== null && idValid(masterId)) {
      return (
        <ChildTabFragment config={config} child={child} masterId={masterId} sp={sp} />
      );
    }
    return <MasterFragment config={config} sp={sp} />;
  }

  return <MasterFullPage config={config} sp={sp} />;
}
