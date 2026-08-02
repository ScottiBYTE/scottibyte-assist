import {
  createHash,
  randomBytes,
  randomUUID
} from 'node:crypto';

import {
  database,
  runImmediateTransaction
} from './database.js';

function hashCredential(credential) {
  return createHash('sha256')
    .update(credential)
    .digest('hex');
}

function normalizeProvider(row) {
  if (!row) {
    return null;
  }

  return {
    id: row.id,
    displayName: row.display_name,
    createdAt: row.created_at,
    lastUsedAt: row.last_used_at,
    revokedAt: row.revoked_at,
    active: row.revoked_at === null
  };
}

function generateCredential() {
  return (
    'assist_provider_' +
    randomBytes(32)
      .toString('base64url')
  );
}

export function createProviderCredential(
  displayName
) {
  const normalizedName =
    typeof displayName === 'string'
      ? displayName.trim().slice(0, 128)
      : '';

  if (!normalizedName) {
    return {
      error: 'display_name_required',
      message:
        'A provider display name is required.'
    };
  }

  const id = randomUUID();
  const credential =
    generateCredential();

  const credentialHash =
    hashCredential(credential);

  const createdAt =
    new Date().toISOString();

  database.prepare(`
    INSERT INTO provider_credentials (
      id,
      display_name,
      credential_hash,
      created_at,
      last_used_at,
      revoked_at
    )
    VALUES (?, ?, ?, ?, NULL, NULL)
  `).run(
    id,
    normalizedName,
    credentialHash,
    createdAt
  );

  return {
    provider: getProviderCredential(id),
    credential
  };
}

export function getProviderCredential(id) {
  const row = database.prepare(`
    SELECT
      id,
      display_name,
      created_at,
      last_used_at,
      revoked_at
    FROM provider_credentials
    WHERE id = ?
  `).get(id);

  return normalizeProvider(row);
}

export function listProviderCredentials() {
  return database.prepare(`
    SELECT
      id,
      display_name,
      created_at,
      last_used_at,
      revoked_at
    FROM provider_credentials
    ORDER BY created_at DESC
  `).all().map(normalizeProvider);
}

export function validateProviderCredential(
  credential
) {
  if (
    typeof credential !== 'string' ||
    !credential
  ) {
    return null;
  }

  const credentialHash =
    hashCredential(credential);

  return runImmediateTransaction(() => {
    const row = database.prepare(`
      SELECT
        id,
        display_name,
        created_at,
        last_used_at,
        revoked_at
      FROM provider_credentials
      WHERE credential_hash = ?
        AND revoked_at IS NULL
    `).get(credentialHash);

    if (!row) {
      return null;
    }

    const lastUsedAt =
      new Date().toISOString();

    database.prepare(`
      UPDATE provider_credentials
      SET last_used_at = ?
      WHERE id = ?
        AND revoked_at IS NULL
    `).run(
      lastUsedAt,
      row.id
    );

    return {
      ...normalizeProvider(row),
      lastUsedAt
    };
  });
}

export function revokeProviderCredential(id) {
  const revokedAt =
    new Date().toISOString();

  const result = database.prepare(`
    UPDATE provider_credentials
    SET revoked_at = ?
    WHERE id = ?
      AND revoked_at IS NULL
  `).run(
    revokedAt,
    id
  );

  if (result.changes !== 1) {
    return {
      error: 'not_found_or_revoked',
      message:
        'The provider credential was not found or was already revoked.'
    };
  }

  return {
    provider:
      getProviderCredential(id)
  };
}
