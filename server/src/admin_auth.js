import {
  randomBytes,
  scryptSync,
  timingSafeEqual
} from 'node:crypto';

import {
  database
} from './database.js';

const saltBytes = 16;
const keyLength = 64;

function passwordHash(
  password,
  salt
) {
  return scryptSync(
    password,
    salt,
    keyLength
  );
}

export function adminPasswordConfigured() {
  const row =
    database.prepare(`
      SELECT id
      FROM admin_auth
      WHERE id = 1
    `).get();

  return Boolean(row);
}

export function setAdminPassword(
  password
) {
  const normalized =
    typeof password === 'string'
      ? password
      : '';

  if (normalized.length < 10) {
    return {
      error:
        'admin_password_too_short',
      message:
        'The administrator password must be at least 10 characters.'
    };
  }

  const salt =
    randomBytes(saltBytes);

  const hash =
    passwordHash(
      normalized,
      salt
    );

  const now =
    new Date().toISOString();

  database.prepare(`
    INSERT INTO admin_auth (
      id,
      password_salt,
      password_hash,
      created_at,
      updated_at
    )
    VALUES (
      1,
      ?,
      ?,
      ?,
      ?
    )
    ON CONFLICT(id)
    DO UPDATE SET
      password_salt = excluded.password_salt,
      password_hash = excluded.password_hash,
      updated_at = excluded.updated_at
  `).run(
    salt.toString('base64'),
    hash.toString('base64'),
    now,
    now
  );

  return {
    configured: true
  };
}

export function verifyAdminPassword(
  password
) {
  if (
    typeof password !== 'string' ||
    password.length === 0
  ) {
    return false;
  }

  const row =
    database.prepare(`
      SELECT
        password_salt,
        password_hash
      FROM admin_auth
      WHERE id = 1
    `).get();

  if (!row) {
    return false;
  }

  const salt =
    Buffer.from(
      row.password_salt,
      'base64'
    );

  const expected =
    Buffer.from(
      row.password_hash,
      'base64'
    );

  const actual =
    passwordHash(
      password,
      salt
    );

  if (
    actual.length !==
    expected.length
  ) {
    return false;
  }

  return timingSafeEqual(
    actual,
    expected
  );
}
