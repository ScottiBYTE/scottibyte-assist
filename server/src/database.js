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
`);

function tableExists(tableName) {
  return Boolean(
    database.prepare(`
      SELECT name
      FROM sqlite_master
      WHERE type = 'table'
        AND name = ?
    `).get(tableName)
  );
}

function createSessionsTable() {
  database.exec(`
    CREATE TABLE sessions (
      id TEXT PRIMARY KEY,
      code TEXT NOT NULL UNIQUE,
      status TEXT NOT NULL CHECK (
        status IN (
          'WAITING',
          'SUPPORTER_JOINED',
          'ENDED',
          'EXPIRED'
        )
      ),
      customer_device_id TEXT,
      customer_token_hash TEXT,
      supporter_device_id TEXT,
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
}

function migrateLegacySessionsTable() {
  if (!tableExists('sessions')) {
    createSessionsTable();
    return;
  }

  const schema = database.prepare(`
    SELECT sql
    FROM sqlite_master
    WHERE type = 'table'
      AND name = 'sessions'
  `).get()?.sql ?? '';

  if (
    schema.includes("'WAITING'") &&
    schema.includes("'SUPPORTER_JOINED'")
  ) {
    return;
  }

  const migrationTime = new Date().toISOString();

  database.exec(`
    BEGIN IMMEDIATE;

    ALTER TABLE sessions
      RENAME TO sessions_legacy_v050;

    CREATE TABLE sessions (
      id TEXT PRIMARY KEY,
      code TEXT NOT NULL UNIQUE,
      status TEXT NOT NULL CHECK (
        status IN (
          'WAITING',
          'SUPPORTER_JOINED',
          'ENDED',
          'EXPIRED'
        )
      ),
      customer_device_id TEXT,
      customer_token_hash TEXT,
      supporter_device_id TEXT,
      created_at TEXT NOT NULL,
      expires_at TEXT NOT NULL,
      joined_at TEXT,
      ended_at TEXT
    ) STRICT;

    INSERT INTO sessions (
      id,
      code,
      status,
      customer_device_id,
      customer_token_hash,
      supporter_device_id,
      created_at,
      expires_at,
      joined_at,
      ended_at
    )
    SELECT
      id,
      code,
      CASE
        WHEN status = 'ENDED' THEN 'ENDED'
        ELSE 'EXPIRED'
      END,
      customer_device_id,
      NULL,
      supporter_device_id,
      created_at,
      expires_at,
      joined_at,
      CASE
        WHEN status = 'ENDED' THEN ended_at
        ELSE '${migrationTime}'
      END
    FROM sessions_legacy_v050;

    DROP TABLE sessions_legacy_v050;

    CREATE INDEX idx_sessions_code
      ON sessions(code);

    CREATE INDEX idx_sessions_status
      ON sessions(status);

    CREATE INDEX idx_sessions_expires_at
      ON sessions(expires_at);

    COMMIT;
  `);
}

migrateLegacySessionsTable();

export function expireOldSessions() {
  const now = new Date().toISOString();

  database.prepare(`
    UPDATE sessions
    SET
      status = 'EXPIRED',
      customer_token_hash = NULL
    WHERE status IN (
      'WAITING',
      'SUPPORTER_JOINED'
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
    customerDeviceId:
      session.customer_device_id,
    supporterDeviceId:
      session.supporter_device_id,
    createdAt: session.created_at,
    expiresAt: session.expires_at,
    joinedAt: session.joined_at,
    endedAt: session.ended_at
  };
}
