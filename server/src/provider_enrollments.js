import {
  createHash,
  randomInt,
  randomUUID
} from 'node:crypto';

import {
  database,
  runImmediateTransaction
} from './database.js';

import {
  createProviderCredential
} from './providers.js';

const enrollmentLifetimeMs =
  15 * 60 * 1000;

function hashCode(code) {
  return createHash('sha256')
    .update(code)
    .digest('hex');
}

function formatCode(value) {
  const digits =
    value.toString()
      .padStart(9, '0');

  return (
    digits.slice(0, 3) +
    '-' +
    digits.slice(3, 6) +
    '-' +
    digits.slice(6, 9)
  );
}

function normalizeCode(value) {
  return typeof value === 'string'
    ? value.replace(/\D/g, '')
    : '';
}

function enrollmentStatus(row) {
  if (row.redeemed_at) {
    return 'redeemed';
  }

  if (row.cancelled_at) {
    return 'cancelled';
  }

  if (
    new Date(row.expires_at).getTime() <=
    Date.now()
  ) {
    return 'expired';
  }

  return 'pending';
}

function normalizeEnrollment(row) {
  return {
    id: row.id,
    displayName:
      row.display_name,
    createdAt:
      row.created_at,
    expiresAt:
      row.expires_at,
    redeemedAt:
      row.redeemed_at,
    cancelledAt:
      row.cancelled_at,
    status:
      enrollmentStatus(row)
  };
}

export function createProviderEnrollment(
  displayName
) {
  const normalizedName =
    typeof displayName === 'string'
      ? displayName.trim().slice(0, 128)
      : '';

  if (!normalizedName) {
    return {
      error:
        'display_name_required',
      message:
        'A provider display name is required.'
    };
  }

  const id =
    randomUUID();

  const code =
    formatCode(
      randomInt(
        0,
        1000000000
      )
    );

  const createdAt =
    new Date();

  const expiresAt =
    new Date(
      createdAt.getTime() +
      enrollmentLifetimeMs
    );

  database.prepare(`
    INSERT INTO provider_enrollments (
      id,
      display_name,
      code_hash,
      created_at,
      expires_at,
      redeemed_at,
      cancelled_at
    )
    VALUES (?, ?, ?, ?, ?, NULL, NULL)
  `).run(
    id,
    normalizedName,
    hashCode(
      normalizeCode(code)
    ),
    createdAt.toISOString(),
    expiresAt.toISOString()
  );

  return {
    enrollment: {
      id,
      displayName:
        normalizedName,
      createdAt:
        createdAt.toISOString(),
      expiresAt:
        expiresAt.toISOString(),
      redeemedAt: null,
      cancelledAt: null,
      status: 'pending'
    },
    enrollmentCode: code
  };
}

export function listProviderEnrollments() {
  return database.prepare(`
    SELECT
      id,
      display_name,
      created_at,
      expires_at,
      redeemed_at,
      cancelled_at
    FROM provider_enrollments
    ORDER BY created_at DESC
  `)
    .all()
    .map(normalizeEnrollment);
}

export function cancelProviderEnrollment(
  id
) {
  const now =
    new Date().toISOString();

  const result =
    database.prepare(`
      UPDATE provider_enrollments
      SET cancelled_at = ?
      WHERE id = ?
        AND redeemed_at IS NULL
        AND cancelled_at IS NULL
    `).run(
      now,
      id
    );

  if (result.changes !== 1) {
    return {
      error:
        'enrollment_not_pending',
      message:
        'The provider enrollment is not pending.'
    };
  }

  return {
    cancelled: true
  };
}

export function redeemProviderEnrollment(
  enrollmentCode
) {
  const normalizedCode =
    normalizeCode(
      enrollmentCode
    );

  if (normalizedCode.length !== 9) {
    return {
      error:
        'invalid_enrollment_code',
      message:
        'The provider enrollment code is not valid.'
    };
  }

  return runImmediateTransaction(() => {
    const row =
      database.prepare(`
        SELECT
          id,
          display_name,
          created_at,
          expires_at,
          redeemed_at,
          cancelled_at
        FROM provider_enrollments
        WHERE code_hash = ?
        LIMIT 1
      `).get(
        hashCode(normalizedCode)
      );

    if (!row) {
      return {
        error:
          'invalid_enrollment_code',
        message:
          'The provider enrollment code is not valid.'
      };
    }

    const status =
      enrollmentStatus(row);

    if (status !== 'pending') {
      return {
        error:
          'enrollment_not_pending',
        message:
          status === 'expired'
            ? 'The provider enrollment code has expired.'
            : 'The provider enrollment code is no longer available.'
      };
    }

    const providerResult =
      createProviderCredential(
        row.display_name,
        'provider'
      );

    if (providerResult.error) {
      throw new Error(
        providerResult.message ||
        providerResult.error
      );
    }

    database.prepare(`
      UPDATE provider_enrollments
      SET redeemed_at = ?
      WHERE id = ?
    `).run(
      new Date().toISOString(),
      row.id
    );

    return {
      provider:
        providerResult.provider,
      credential:
        providerResult.credential
    };
  });
}
