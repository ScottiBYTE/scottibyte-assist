import {
  createHash,
  timingSafeEqual
} from 'node:crypto';

import {
  validateProviderCredential
} from './providers.js';

const configuredToken =
  process.env.SUPPORTER_API_TOKEN ?? '';

function digest(value) {
  return createHash('sha256')
    .update(value)
    .digest();
}

function tokensMatch(
  receivedToken,
  expectedToken
) {
  if (!receivedToken || !expectedToken) {
    return false;
  }

  return timingSafeEqual(
    digest(receivedToken),
    digest(expectedToken)
  );
}

export function authenticateSupporterToken(
  token)
{
  if (
    tokensMatch(
      token,
      configuredToken)
  ) {
    return {
      authenticated: true,
      type: 'master',
      role: 'superuser',
      providerId: null,
      displayName:
        'Assist Server Administrator'
    };
  }

  const provider =
    validateProviderCredential(token);

  if (!provider) {
    return null;
  }

  return {
    authenticated: true,
    type: 'provider',
    role: provider.role,
    providerId: provider.id,
    displayName:
      provider.displayName
  };
}

export function validateSupporterToken(
  token)
{
  return Boolean(
    authenticateSupporterToken(token)
  );
}

export function supporterAuthConfigured()
{
  /*
   * The master token remains required for
   * provider administration and bootstrap.
   */
  return configuredToken.length > 0;
}

export function requireMasterSupporter(
  request,
  response,
  next)
{
  if (!supporterAuthConfigured()) {
    console.error(
      'SUPPORTER_API_TOKEN is not configured'
    );

    return response.status(503).json({
      error:
        'supporter_auth_unavailable',
      message:
        'Supporter authentication is not configured.'
    });
  }

  const authorization =
    request.headers.authorization ?? '';

  const match = authorization.match(
    /^Bearer\s+(.+)$/i
  );

  if (!match) {
    return response.status(401).json({
      error:
        'authorization_required',
      message:
        'An administrator bearer token is required.'
    });
  }

  if (
    !tokensMatch(
      match[1],
      configuredToken)
  ) {
    return response.status(403).json({
      error:
        'invalid_administrator_token',
      message:
        'The administrator token is not valid.'
    });
  }

  request.supporter = {
    authenticated: true,
    type: 'master',
    providerId: null,
    displayName:
      'Assist Server Administrator'
  };

  next();
}

export function requireSuperuser(
  request,
  response,
  next)
{
  const authorization =
    request.headers.authorization ?? '';

  const match = authorization.match(
    /^Bearer\s+(.+)$/i
  );

  if (!match) {
    return response.status(401).json({
      error:
        'authorization_required',
      message:
        'A superuser bearer token is required.'
    });
  }

  const supporter =
    authenticateSupporterToken(
      match[1]);

  if (!supporter) {
    return response.status(403).json({
      error:
        'invalid_supporter_token',
      message:
        'The provider credential is not valid.'
    });
  }

  if (supporter.role !== 'superuser') {
    return response.status(403).json({
      error:
        'superuser_required',
      message:
        'Provider administration requires '
        + 'a superuser credential.'
    });
  }

  request.supporter = supporter;

  next();
}

export function requireSupporter(
  request,
  response,
  next)
{
  const authorization =
    request.headers.authorization ?? '';

  const match = authorization.match(
    /^Bearer\s+(.+)$/i
  );

  if (!match) {
    return response.status(401).json({
      error:
        'authorization_required',
      message:
        'A supporter bearer token is required.'
    });
  }

  const supporter =
    authenticateSupporterToken(
      match[1]);

  if (!supporter) {
    return response.status(403).json({
      error:
        'invalid_supporter_token',
      message:
        'The supporter token is not valid.'
    });
  }

  request.supporter = supporter;

  next();
}
