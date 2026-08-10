import {
  randomInt,
  timingSafeEqual
} from 'node:crypto';

import {
  database,
  runImmediateTransaction
} from './database.js';

import {
  createProviderCredential
} from './providers.js';

import {
  setAdminPassword
} from './admin_auth.js';

let bootstrapCode = null;

function formatCode(value) {
  const digits =
    value.toString().padStart(9, '0');

  return (
    digits.slice(0, 3) +
    '-' +
    digits.slice(3, 6) +
    '-' +
    digits.slice(6, 9)
  );
}

function normalizedCode(value) {
  return typeof value === 'string'
    ? value.replace(/\D/g, '')
    : '';
}

function codesMatch(
  received,
  expected
) {
  const left =
    Buffer.from(
      normalizedCode(received));

  const right =
    Buffer.from(
      normalizedCode(expected));

  if (
    left.length === 0 ||
    left.length !== right.length
  ) {
    return false;
  }

  return timingSafeEqual(
    left,
    right
  );
}

function providerCount() {
  return Number(
    database.prepare(`
      SELECT COUNT(*) AS count
      FROM provider_credentials
    `).get()?.count ?? 0
  );
}

export function bootstrapRequired() {
  return providerCount() === 0;
}

export function initializeBootstrap() {
  if (!bootstrapRequired()) {
    bootstrapCode = null;
    return false;
  }

  bootstrapCode =
    formatCode(
      randomInt(
        0,
        1000000000
      )
    );

  console.log('');
  console.log(
    '===================================================='
  );
  console.log(
    'ScottiBYTE Assist — Initial Provider Setup'
  );
  console.log('');
  console.log(
    'No provider administrator exists.'
  );
  console.log('');
  console.log(
    `One-time setup code:  ${bootstrapCode}`
  );
  console.log('');
  console.log(
    'Enter this code on the first computer that will'
  );
  console.log(
    'administer ScottiBYTE Assist.'
  );
  console.log('');
  console.log(
    'This code becomes invalid after first enrollment.'
  );
  console.log(
    '===================================================='
  );
  console.log('');

  return true;
}

export function bootstrapStatus() {
  return {
    bootstrapRequired:
      bootstrapRequired()
  };
}

export function redeemBootstrap(
  setupCode,
  displayName,
  adminPassword
) {
  if (!bootstrapRequired()) {
    return {
      error:
        'bootstrap_not_available',
      message:
        'Initial provider setup has already been completed.'
    };
  }

  if (
    !bootstrapCode ||
    !codesMatch(
      setupCode,
      bootstrapCode
    )
  ) {
    return {
      error:
        'invalid_setup_code',
      message:
        'The setup code is not valid.'
    };
  }

  if (
    typeof adminPassword !== 'string' ||
    adminPassword.length < 10
  ) {
    return {
      error:
        'admin_password_too_short',
      message:
        'The administrator password must be at least 10 characters.'
    };
  }

  return runImmediateTransaction(() => {
    if (providerCount() !== 0) {
      bootstrapCode = null;

      return {
        error:
          'bootstrap_not_available',
        message:
          'Initial provider setup has already been completed.'
      };
    }

    const result =
      createProviderCredential(
        displayName,
        'superuser'
      );

    if (result.error) {
      return result;
    }

    const passwordResult =
      setAdminPassword(
        adminPassword
      );

    if (passwordResult.error) {
      throw new Error(
        passwordResult.message ||
        passwordResult.error
      );
    }

    bootstrapCode = null;

    return {
      provider:
        result.provider,
      credential:
        result.credential
    };
  });
}
