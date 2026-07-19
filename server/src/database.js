import { mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import { DatabaseSync } from 'node:sqlite';

const databasePath =
  process.env.DATABASE_PATH ?? '/app/data/assist.sqlite';

mkdirSync(dirname(databasePath), {
  recursive: true
});

export const database =
  new DatabaseSync(databasePath);

database.exec(`
  PRAGMA journal_mode = WAL;
  PRAGMA foreign_keys = ON;
  PRAGMA busy_timeout = 5000;

  CREATE TABLE IF NOT EXISTS sessions (
    id TEXT PRIMARY KEY,
    code TEXT NOT NULL UNIQUE,
    status TEXT NOT NULL CHECK (
      status IN (
        'CREATED',
        'CUSTOMER_JOINED',
        'ENDED',
        'EXPIRED'
      )
    ),
    supporter_device_id TEXT,
    customer_device_id TEXT,
    customer_token_hash TEXT,
    created_at TEXT NOT NULL,
    expires_at TEXT NOT NULL,
    joined_at TEXT,
    ended_at TEXT
  ) STRICT;

  CREATE INDEX IF NOT EXISTS idx_sessions_code
    ON sessions(code);

  CREATE INDEX IF NOT EXISTS idx_sessions_status
    ON sessions(status);

  CREATE INDEX IF NOT EXISTS idx_sessions_expires_at
    ON sessions(expires_at);
`);

function ensureColumn(
  tableName,
  columnName,
  definition
) {
  const columns = database
    .prepare(`PRAGMA table_info(${tableName})`)
    .all();

  const exists = columns.some(
    (column) => column.name === columnName
  );

  if (!exists) {
    database.exec(`
      ALTER TABLE ${tableName}
      ADD COLUMN ${columnName} ${definition}
    `);
  }
}

/*
 * Migration for databases created by v0.2-v0.4.
 */
ensureColumn(
  'sessions',
  'customer_token_hash',
  'TEXT'
);

export function expireOldSessions() {
  const now = new Date().toISOString();

  database.prepare(`
    UPDATE sessions
    SET status = 'EXPIRED'
    WHERE status IN (
      'CREATED',
      'CUSTOMER_JOINED'
    )
      AND expires_at <= ?
  `).run(now);
}

export function normalizeSession(session) {
  if (!session) {
    return null;
  }

  return {
    id: session.id,
    code: session.code,
    status: session.status,
    supporterDeviceId:
      session.supporter_device_id,
    customerDeviceId:
      session.customer_device_id,
    createdAt: session.created_at,
    expiresAt: session.expires_at,
    joinedAt: session.joined_at,
    endedAt: session.ended_at
  };
}
