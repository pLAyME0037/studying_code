// SQLite access via node:sqlite (built into Node 26: sync, zero native deps
// — ideal for Server Components). Port of webc_pos's src/db/db.c open +
// migrate path: connection PRAGMAs before any transaction, migrations
// applied in one transaction with content-matched history rows.
import { DatabaseSync } from "node:sqlite";
import { readFileSync, readdirSync, mkdirSync, existsSync } from "node:fs";
import path from "node:path";

const DB_DIR = path.join(process.cwd(), "data");
const DB_PATH = path.join(DB_DIR, "webc.db");
const MIGRATIONS_DIR = path.join(
  process.cwd(),
  "app/lib/db/migrations",
);

// Ordered migration file names (0001..0012), read from disk on first open.
function migrationFiles(): { name: string; sql: string }[] {
  const files = readdirSync(MIGRATIONS_DIR)
    .filter((f) => f.endsWith(".sql"))
    .sort();
  return files.map((name) => ({
    name,
    sql: readFileSync(path.join(MIGRATIONS_DIR, name), "utf8"),
  }));
}

// Connection setup — everything SQLite refuses to run inside a transaction
// (webc_pos driver_sqlite.c: WAL journal, NORMAL sync, 2000 KB page cache,
// FILE temp tables, no mmap). foreign_keys is handled by the caller.
// busy_timeout MUST lead: `next build` collects page data in several
// worker processes that open this file at the same time — without it the
// WAL pragma (or a migration write) fails with SQLITE_BUSY mid-build.
function applyConnectionPragmas(db: DatabaseSync): void {
  db.exec("PRAGMA busy_timeout = 5000;");
  db.exec("PRAGMA journal_mode = WAL;");
  db.exec("PRAGMA synchronous = NORMAL;");
  db.exec("PRAGMA cache_size = -2000;");
  db.exec("PRAGMA temp_store = 1;");
  db.exec("PRAGMA mmap_size = 0;");
}

const MIGRATIONS_TABLE_SQL = `CREATE TABLE IF NOT EXISTS Migrations (
    applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    query TEXT NOT NULL
);`;

// Port of webc_pos create_schema_status(): history rows are matched BY
// CONTENT (first-fit) against the files — no reliance on row order, an
// edited file can never silently pass. Unmatched rows = history this
// checkout does not own -> hard error (webc_pos rebuilds; this port is a
// fresh-project DB, so failing loudly beats silently rebuilding data).
function migrate(db: DatabaseSync): void {
  db.exec(MIGRATIONS_TABLE_SQL);

  const stored: string[] = db
    .prepare("SELECT query FROM Migrations;")
    .all()
    .map((r) => String((r as { query: unknown }).query ?? ""));

  const files = migrationFiles();
  if (stored.length > files.length) {
    throw new Error(
      `webc.db: database schema is too new (${stored.length} history rows ` +
        `> ${files.length} migration files). Update the application.`,
    );
  }

  const storedUsed = new Array<boolean>(stored.length).fill(false);
  const matched = new Array<boolean>(files.length).fill(false);
  for (let i = 0; i < files.length; ++i) {
    for (let j = 0; j < stored.length; ++j) {
      if (storedUsed[j]) continue;
      if (stored[j] === files[i].sql) {
        storedUsed[j] = true;
        matched[i] = true;
        break;
      }
    }
  }

  for (let j = 0; j < stored.length; ++j) {
    if (storedUsed[j]) continue;
    const i = matched.findIndex((m) => !m);
    const name = i >= 0 ? files[i].name : "?";
    throw new Error(
      `webc.db: invalid database schema — mismatch in migration ${name}.\n` +
        `FOUND: ${stored[j] ? stored[j].slice(0, 500) : "(null)"}`,
    );
  }

  const pending = files.filter((_, i) => !matched[i]);
  if (pending.length === 0) return;

  // Migrations may swap tables other tables point at (0003 rebuilds `users`
  // while notes.user_id references it): SQLite rejects DROP/RENAME against
  // live FKs and PRAGMA foreign_keys is a no-op inside a transaction, so
  // toggle it around the whole apply (webc_pos db.c same pattern).
  db.exec("PRAGMA foreign_keys=OFF;");
  db.exec("BEGIN");
  try {
    for (const mig of pending) {
      console.log(`INFO: webc.db: applying migration ${mig.name}`);
      db.exec(mig.sql);
      db.prepare("INSERT INTO Migrations (query) VALUES (?)").run(mig.sql);
    }
    db.exec("COMMIT");
  } catch (err) {
    db.exec("ROLLBACK");
    throw err;
  } finally {
    db.exec("PRAGMA foreign_keys=ON;");
  }
}

function open(): DatabaseSync {
  if (!existsSync(DB_DIR)) mkdirSync(DB_DIR, { recursive: true });
  const db = new DatabaseSync(DB_PATH);
  applyConnectionPragmas(db);
  migrate(db);
  db.exec("PRAGMA foreign_keys=ON;");
  return db;
}

// Module-scope singleton: survives HMR via globalThis, one connection for
// the whole process (sync queries in RSC never block on pool checkout).
const g = globalThis as typeof globalThis & { __webcDb?: DatabaseSync };
export const db: DatabaseSync = g.__webcDb ?? (g.__webcDb = open());

// Small typed helpers over node:sqlite's null-prototype rows.
export function queryOne<T>(sql: string, ...params: unknown[]): T | undefined {
  return db.prepare(sql).get(...(params as never[])) as T | undefined;
}

export function queryAll<T>(sql: string, ...params: unknown[]): T[] {
  return db.prepare(sql).all(...(params as never[])) as T[];
}
