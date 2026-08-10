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
    role: row.role,
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
  displayName,
  role = 'provider'
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

  const normalizedRole =
    role === 'superuser'
      ? 'superuser'
      : 'provider';

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
      role,
      created_at,
      last_used_at,
      revoked_at
    )
    VALUES (?, ?, ?, ?, ?, NULL, NULL)
  `).run(
    id,
    normalizedName,
    credentialHash,
    normalizedRole,
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
      role,
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
      role,
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
        role,
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

export function renameProviderCredential(
  id,
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

  const result = database.prepare(`
    UPDATE provider_credentials
    SET display_name = ?
    WHERE id = ?
  `).run(
    normalizedName,
    id
  );

  if (result.changes !== 1) {
    return {
      error: 'provider_not_found',
      message:
        'The provider was not found.'
    };
  }

  return {
    provider:
      getProviderCredential(id)
  };
}

export function revokeProviderCredential(id) {
  const provider =
    getProviderCredential(id);

  if (
    provider == null ||
    provider.revokedAt != null
  ) {
    return {
      error: 'not_found_or_revoked',
      message:
        'The provider credential was not found or was already revoked.'
    };
  }

  if (provider.role === 'superuser') {
    const row = database.prepare(`
      SELECT COUNT(*) AS count
      FROM provider_credentials
      WHERE role = 'superuser'
        AND revoked_at IS NULL
    `).get();

    if (Number(row.count) <= 1) {
      return {
        error: 'last_superuser',
        message:
          'The last active superuser cannot be revoked.'
      };
    }
  }

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
