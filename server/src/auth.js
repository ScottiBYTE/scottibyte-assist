import {
  createHash,
  timingSafeEqual
} from 'node:crypto';

const configuredToken =
  process.env.SUPPORTER_API_TOKEN ?? '';

function digest(value) {
  return createHash('sha256')
    .update(value)
    .digest();
}

function tokensMatch(receivedToken, expectedToken) {
  if (!receivedToken || !expectedToken) {
    return false;
  }

  return timingSafeEqual(
    digest(receivedToken),
    digest(expectedToken)
  );
}

export function validateSupporterToken(token) {
  return tokensMatch(token, configuredToken);
}

export function supporterAuthConfigured() {
  return configuredToken.length > 0;
}

export function requireSupporter(
  request,
  response,
  next
) {
  if (!supporterAuthConfigured()) {
    console.error(
      'SUPPORTER_API_TOKEN is not configured'
    );

    return response.status(503).json({
      error: 'supporter_auth_unavailable',
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
      error: 'authorization_required',
      message:
        'A supporter bearer token is required.'
    });
  }

  if (!validateSupporterToken(match[1])) {
    return response.status(403).json({
      error: 'invalid_supporter_token',
      message:
        'The supporter token is not valid.'
    });
  }

  request.supporter = {
    authenticated: true
  };

  next();
}
