import {
  createHash,
  randomBytes,
  randomInt,
  randomUUID,
  timingSafeEqual
} from 'node:crypto';

import {
  database,
  expireOldSessions,
  normalizeSession
} from './database.js';

const SESSION_LIFETIME_MINUTES =
  Number.parseInt(
    process.env.SESSION_LIFETIME_MINUTES ??
      '30',
    10
  );

function hashToken(token) {
  return createHash('sha256')
    .update(token)
    .digest('hex');
}

function safeHashMatch(
  suppliedToken,
  storedHash
) {
  if (
    typeof suppliedToken !== 'string' ||
    typeof storedHash !== 'string'
  ) {
    return false;
  }

  const suppliedHash =
    Buffer.from(
      hashToken(suppliedToken),
      'hex'
    );

  const expectedHash =
    Buffer.from(storedHash, 'hex');

  if (
    suppliedHash.length !==
    expectedHash.length
  ) {
    return false;
  }

  return timingSafeEqual(
    suppliedHash,
    expectedHash
  );
}

function generateCode() {
  return randomInt(0, 1_000_000)
    .toString()
    .padStart(6, '0');
}

function generateCustomerToken() {
  return randomBytes(32).toString('hex');
}

function createUniqueCode() {
  const findByCode = database.prepare(`
    SELECT code
    FROM sessions
    WHERE code = ?
  `);

  for (
    let attempt = 0;
    attempt < 20;
    attempt += 1
  ) {
    const code = generateCode();

    if (!findByCode.get(code)) {
      return code;
    }
  }

  throw new Error(
    'Unable to generate a unique support code'
  );
}

export function createCustomerSession({
  customerDeviceId = null
} = {}) {
  expireOldSessions();

  const id = randomUUID();
  const code = createUniqueCode();
  const customerToken =
    generateCustomerToken();
  const customerTokenHash =
    hashToken(customerToken);
  const createdAt = new Date();

  const expiresAt = new Date(
    createdAt.getTime() +
      SESSION_LIFETIME_MINUTES *
        60 *
        1000
  );

  database.prepare(`
    INSERT INTO sessions (
      id,
      code,
      status,
      customer_device_id,
      customer_token_hash,
      created_at,
      expires_at
    )
    VALUES (
      ?,
      ?,
      'WAITING',
      ?,
      ?,
      ?,
      ?
    )
  `).run(
    id,
    code,
    customerDeviceId,
    customerTokenHash,
    createdAt.toISOString(),
    expiresAt.toISOString()
  );

  return {
    session: getSession(code),
    customerToken
  };
}

export function getSession(code) {
  expireOldSessions();

  const session = database.prepare(`
    SELECT *
    FROM sessions
    WHERE code = ?
  `).get(code);

  return normalizeSession(session);
}

export function claimSession(
  code,
  {
    supporterDeviceId = null
  } = {}
) {
  expireOldSessions();

  const session = getSession(code);

  if (!session) {
    return {
      error: 'not_found',
      message:
        'The support code is not valid.'
    };
  }

  if (session.status === 'EXPIRED') {
    return {
      error: 'expired',
      message:
        'The support code has expired.'
    };
  }

  if (
    session.status ===
    'SUPPORTER_JOINED'
  ) {
    return {
      error: 'already_claimed',
      message:
        'A supporter is already connected to this session.'
    };
  }

  if (session.status === 'ENDED') {
    return {
      error: 'ended',
      message:
        'This support session has ended.'
    };
  }

  const joinedAt =
    new Date().toISOString();

  const result = database.prepare(`
    UPDATE sessions
    SET
      status = 'SUPPORTER_JOINED',
      supporter_device_id = ?,
      joined_at = ?
    WHERE code = ?
      AND status = 'WAITING'
  `).run(
    supporterDeviceId,
    joinedAt,
    code
  );

  if (result.changes !== 1) {
    return {
      error: 'state_conflict',
      message:
        'The session state changed before it could be claimed.'
    };
  }

  return {
    session: getSession(code)
  };
}

export function validateCustomerToken(
  code,
  customerToken
) {
  expireOldSessions();

  const row = database.prepare(`
    SELECT
      status,
      customer_token_hash
    FROM sessions
    WHERE code = ?
  `).get(code);

  if (!row) {
    return false;
  }

  if (
    ![
      'WAITING',
      'SUPPORTER_JOINED'
    ].includes(row.status)
  ) {
    return false;
  }

  return safeHashMatch(
    customerToken,
    row.customer_token_hash
  );
}

export function endSession(code) {
  expireOldSessions();

  const session = getSession(code);

  if (!session) {
    return {
      error: 'not_found',
      message:
        'The support code is not valid.'
    };
  }

  if (session.status === 'ENDED') {
    return {
      error: 'already_ended',
      message:
        'This support session has already ended.'
    };
  }

  if (session.status === 'EXPIRED') {
    return {
      error: 'expired',
      message:
        'The support session has expired.'
    };
  }

  const endedAt =
    new Date().toISOString();

  const result = database.prepare(`
    UPDATE sessions
    SET
      status = 'ENDED',
      ended_at = ?,
      customer_token_hash = NULL
    WHERE code = ?
      AND status IN (
        'WAITING',
        'SUPPORTER_JOINED'
      )
  `).run(
    endedAt,
    code
  );

  if (result.changes !== 1) {
    return {
      error: 'state_conflict',
      message:
        'The session state changed before it could be ended.'
    };
  }

  return {
    session: getSession(code)
  };
}
