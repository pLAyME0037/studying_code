// Minimal ambient types for node:sqlite (built into Node 26). @types/node
// in this project is still ^20 and doesn't ship sqlite.d.ts; only the
// surface app/lib/db.ts uses is declared here.
declare module "node:sqlite" {
  export type SupportedValueType = null | number | bigint | string | Uint8Array;

  export interface StatementSync {
    get(...params: SupportedValueType[]): Record<string, unknown> | undefined;
    all(...params: SupportedValueType[]): Record<string, unknown>[];
    run(...params: SupportedValueType[]): {
      changes: number | bigint;
      lastInsertRowid: number | bigint;
    };
  }

  export interface DatabaseSyncOptions {
    open?: boolean;
    readOnly?: boolean;
  }

  export class DatabaseSync {
    constructor(path: string, options?: DatabaseSyncOptions);
    exec(sql: string): void;
    prepare(sql: string): StatementSync;
    close(): void;
  }
}
