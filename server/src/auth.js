import {
  validateProviderCredential
} from './providers.js';

export function authenticateSupporterToken(
  token)
{
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
