import {
  createHash,
  randomUUID
} from 'node:crypto';
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
      receipt_token_hash TEXT,
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
      receipt_token_hash TEXT,
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
      receipt_token_hash,
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

function columnExists(
  tableName,
  columnName
) {
  return database.prepare(`
    SELECT name
    FROM pragma_table_info(?)
    WHERE name = ?
  `).get(
    tableName,
    columnName
  );
}

function ensureReceiptTokenColumn() {
  if (
    columnExists(
      'sessions',
      'receipt_token_hash'
    )
  ) {
    return;
  }

  database.exec(`
    ALTER TABLE sessions
    ADD COLUMN receipt_token_hash TEXT;
  `);
}

function createProviderCredentialsTable() {
  database.exec(`
    CREATE TABLE IF NOT EXISTS provider_credentials (
      id TEXT PRIMARY KEY,
      display_name TEXT NOT NULL,
      credential_hash TEXT NOT NULL UNIQUE,
      role TEXT NOT NULL DEFAULT 'provider' CHECK (
        role IN (
          'superuser',
          'provider'
        )
      ),
      created_at TEXT NOT NULL,
      last_used_at TEXT,
      revoked_at TEXT
    ) STRICT;

    CREATE INDEX IF NOT EXISTS
      idx_provider_credentials_active
      ON provider_credentials(
        revoked_at
      );

    CREATE INDEX IF NOT EXISTS
      idx_provider_credentials_created_at
      ON provider_credentials(
        created_at
      );
  `);
}

function ensureProviderRoleColumn() {
  if (
    !columnExists(
      'provider_credentials',
      'role'
    )
  ) {
    database.exec(`
      ALTER TABLE provider_credentials
      ADD COLUMN role TEXT NOT NULL
        DEFAULT 'provider'
        CHECK (
          role IN (
            'superuser',
            'provider'
          )
        );
    `);
  }

  const existingSuperuser =
    database.prepare(`
      SELECT id
      FROM provider_credentials
      WHERE role = 'superuser'
        AND revoked_at IS NULL
      LIMIT 1
    `).get();

  if (existingSuperuser) {
    return;
  }

  const oldestActiveProvider =
    database.prepare(`
      SELECT id
      FROM provider_credentials
      WHERE revoked_at IS NULL
      ORDER BY created_at ASC
      LIMIT 1
    `).get();

  if (!oldestActiveProvider) {
    return;
  }

  database.prepare(`
    UPDATE provider_credentials
    SET role = 'superuser'
    WHERE id = ?
  `).run(
    oldestActiveProvider.id
  );
}

function createProviderEnrollmentsTable() {
  database.exec(`
    CREATE TABLE IF NOT EXISTS provider_enrollments (
      id TEXT PRIMARY KEY,
      display_name TEXT NOT NULL,
      code_hash TEXT NOT NULL UNIQUE,
      created_at TEXT NOT NULL,
      expires_at TEXT NOT NULL,
      redeemed_at TEXT,
      cancelled_at TEXT
    ) STRICT;

    CREATE INDEX IF NOT EXISTS
      idx_provider_enrollments_status
      ON provider_enrollments(
        redeemed_at,
        cancelled_at,
        expires_at
      );
  `);
}

function createAdminAuthTable() {
  database.exec(`
    CREATE TABLE IF NOT EXISTS admin_auth (
      id INTEGER PRIMARY KEY CHECK (
        id = 1
      ),
      password_salt TEXT NOT NULL,
      password_hash TEXT NOT NULL,
      created_at TEXT NOT NULL,
      updated_at TEXT NOT NULL
    ) STRICT;
  `);
}

function createAuditTable() {
  database.exec(`
    CREATE TABLE IF NOT EXISTS session_audit_events (
      id TEXT PRIMARY KEY,
      session_id TEXT NOT NULL,
      sequence INTEGER NOT NULL CHECK (
        sequence > 0
      ),
      occurred_at TEXT NOT NULL,
      event_type TEXT NOT NULL,
      actor_role TEXT NOT NULL CHECK (
        actor_role IN (
          'customer',
          'supporter',
          'server'
        )
      ),
      actor_id TEXT,
      metadata_json TEXT NOT NULL,
      previous_hash TEXT,
      event_hash TEXT NOT NULL,
      FOREIGN KEY(session_id)
        REFERENCES sessions(id)
        ON DELETE CASCADE,
      UNIQUE(session_id, sequence)
    ) STRICT;

    CREATE INDEX IF NOT EXISTS idx_audit_session_sequence
      ON session_audit_events(
        session_id,
        sequence
      );

    CREATE INDEX IF NOT EXISTS idx_audit_occurred_at
      ON session_audit_events(
        occurred_at
      );

    CREATE INDEX IF NOT EXISTS idx_audit_event_type
      ON session_audit_events(
        event_type
      );
  `);
}

migrateLegacySessionsTable();
ensureReceiptTokenColumn();
createProviderCredentialsTable();
ensureProviderRoleColumn();
createProviderEnrollmentsTable();
createAdminAuthTable();
createAuditTable();

function canonicalize(value) {
  if (Array.isArray(value)) {
    return value.map(canonicalize);
  }

  if (
    value &&
    typeof value === 'object'
  ) {
    return Object.fromEntries(
      Object.keys(value)
        .sort()
        .filter(
          (key) =>
            value[key] !== undefined
        )
        .map((key) => [
          key,
          canonicalize(value[key])
        ])
    );
  }

  return value;
}

function calculateEventHash({
  sessionId,
  sequence,
  occurredAt,
  eventType,
  actorRole,
  actorId,
  metadataJson,
  previousHash
}) {
  return createHash('sha256')
    .update(JSON.stringify({
      sessionId,
      sequence,
      occurredAt,
      eventType,
      actorRole,
      actorId: actorId ?? null,
      metadataJson,
      previousHash: previousHash ?? null
    }))
    .digest('hex');
}

export function appendAuditEventInTransaction({
  sessionId,
  eventType,
  actorRole,
  actorId = null,
  metadata = {}
}) {
  if (
    typeof sessionId !== 'string' ||
    !sessionId
  ) {
    throw new TypeError(
      'A session ID is required.'
    );
  }

  if (
    typeof eventType !== 'string' ||
    !eventType
  ) {
    throw new TypeError(
      'An audit event type is required.'
    );
  }

  if (
    ![
      'customer',
      'supporter',
      'server'
    ].includes(actorRole)
  ) {
    throw new TypeError(
      `Invalid audit actor role: ${actorRole}`
    );
  }

  const sessionExists =
    database.prepare(`
      SELECT id
      FROM sessions
      WHERE id = ?
    `).get(sessionId);

  if (!sessionExists) {
    throw new Error(
      `Session does not exist: ${sessionId}`
    );
  }

  const previous =
    database.prepare(`
      SELECT
        sequence,
        event_hash
      FROM session_audit_events
      WHERE session_id = ?
      ORDER BY sequence DESC
      LIMIT 1
    `).get(sessionId);

  const sequence =
    (previous?.sequence ?? 0) + 1;

  const occurredAt =
    new Date().toISOString();

  const normalizedMetadata =
    canonicalize(metadata ?? {});

  const metadataJson =
    JSON.stringify(normalizedMetadata);

  const previousHash =
    previous?.event_hash ?? null;

  const eventHash =
    calculateEventHash({
      sessionId,
      sequence,
      occurredAt,
      eventType,
      actorRole,
      actorId,
      metadataJson,
      previousHash
    });

  const id = randomUUID();

  database.prepare(`
    INSERT INTO session_audit_events (
      id,
      session_id,
      sequence,
      occurred_at,
      event_type,
      actor_role,
      actor_id,
      metadata_json,
      previous_hash,
      event_hash
    )
    VALUES (
      ?,
      ?,
      ?,
      ?,
      ?,
      ?,
      ?,
      ?,
      ?,
      ?
    )
  `).run(
    id,
    sessionId,
    sequence,
    occurredAt,
    eventType,
    actorRole,
    actorId,
    metadataJson,
    previousHash,
    eventHash
  );

  return {
    id,
    sessionId,
    sequence,
    occurredAt,
    eventType,
    actorRole,
    actorId,
    metadata: normalizedMetadata,
    previousHash,
    eventHash
  };
}

export function runImmediateTransaction(
  operation
) {
  if (typeof operation !== 'function') {
    throw new TypeError(
      'A transaction operation is required.'
    );
  }

  database.exec('BEGIN IMMEDIATE;');

  try {
    const result = operation();

    database.exec('COMMIT;');

    return result;
  } catch (error) {
    database.exec('ROLLBACK;');
    throw error;
  }
}

export function appendAuditEvent({
  sessionId,
  eventType,
  actorRole,
  actorId = null,
  metadata = {}
}) {
  database.exec('BEGIN IMMEDIATE;');

  try {
    const event =
      appendAuditEventInTransaction({
        sessionId,
        eventType,
        actorRole,
        actorId,
        metadata
      });

    database.exec('COMMIT;');

    return event;
  } catch (error) {
    database.exec('ROLLBACK;');
    throw error;
  }
}

export function getAuditEventsBySessionId(
  sessionId
) {
  return database.prepare(`
    SELECT
      id,
      session_id,
      sequence,
      occurred_at,
      event_type,
      actor_role,
      actor_id,
      metadata_json,
      previous_hash,
      event_hash
    FROM session_audit_events
    WHERE session_id = ?
    ORDER BY sequence ASC
  `).all(sessionId).map((row) => ({
    id: row.id,
    sessionId: row.session_id,
    sequence: row.sequence,
    occurredAt: row.occurred_at,
    eventType: row.event_type,
    actorRole: row.actor_role,
    actorId: row.actor_id,
    metadata:
      JSON.parse(row.metadata_json),
    previousHash: row.previous_hash,
    eventHash: row.event_hash
  }));
}

export function verifyAuditChain(
  sessionId
) {
  const events =
    getAuditEventsBySessionId(
      sessionId
    );

  let previousHash = null;

  for (const event of events) {
    if (
      event.previousHash !==
      previousHash
    ) {
      return {
        valid: false,
        sequence: event.sequence,
        error:
          'previous_hash_mismatch'
      };
    }

    const metadataJson =
      JSON.stringify(
        canonicalize(event.metadata)
      );

    const expectedHash =
      calculateEventHash({
        sessionId: event.sessionId,
        sequence: event.sequence,
        occurredAt: event.occurredAt,
        eventType: event.eventType,
        actorRole: event.actorRole,
        actorId: event.actorId,
        metadataJson,
        previousHash
      });

    if (
      event.eventHash !==
      expectedHash
    ) {
      return {
        valid: false,
        sequence: event.sequence,
        error:
          'event_hash_mismatch'
      };
    }

    previousHash = event.eventHash;
  }

  return {
    valid: true,
    eventCount: events.length,
    finalHash: previousHash
  };
}

export function expireOldSessions() {
  return 0;
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
