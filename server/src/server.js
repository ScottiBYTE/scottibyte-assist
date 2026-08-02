import http from 'node:http';
import express from 'express';

import {
  requireMasterSupporter,
  requireSupporter,
  validateSupporterToken
} from './auth.js';

import {
  claimSession,
  createCustomerSession,
  endSession,
  getSession,
  validateCustomerToken,
  validateReceiptToken
} from './sessions.js';

import {
  createProviderCredential,
  listProviderCredentials,
  revokeProviderCredential
} from './providers.js';

import {
  getAuditEventsBySessionId,
  verifyAuditChain
} from './database.js';

import {
  broadcastSessionEvent,
  createWebSocketServer,
  websocketStats
} from './websocket.js';

const app = express();

const port = Number.parseInt(
  process.env.PORT ?? '3089',
  10
);

const host =
  process.env.HOST ?? '0.0.0.0';

app.disable('x-powered-by');

/*
 * Nginx Proxy Manager is the single trusted reverse proxy.
 * This allows request.ip to use the client address supplied
 * through X-Forwarded-For rather than recording the proxy.
 */
app.set('trust proxy', 1);

app.use(express.json({
  limit: '32kb'
}));

function validCode(code) {
  return /^\d{6}$/.test(code);
}

function bearerToken(request) {
  const authorization =
    request.headers.authorization ?? '';

  const match = authorization.match(
    /^Bearer\s+(.+)$/i
  );

  return match?.[1] ?? '';
}

function customerToken(request) {
  const headerToken =
    request.headers['x-customer-token'];

  if (typeof headerToken === 'string') {
    return headerToken;
  }

  if (
    typeof request.body?.customerToken ===
    'string'
  ) {
    return request.body.customerToken;
  }

  return '';
}

function receiptToken(request) {
  const headerToken =
    request.headers['x-receipt-token'];

  if (typeof headerToken === 'string') {
    return headerToken;
  }

  return '';
}

function receiptAuditEvent(event) {
  const metadata = {
    ...event.metadata
  };

  delete metadata.sourceIp;
  delete metadata.clientId;

  return {
    sequence: event.sequence,
    occurredAt: event.occurredAt,
    eventType: event.eventType,
    actorRole: event.actorRole,
    actorId: event.actorId,
    metadata,
    previousHash: event.previousHash,
    eventHash: event.eventHash
  };
}

function sourceIp(request) {
  return request.ip ?? null;
}

function sendSessionError(
  response,
  result
) {
  const statusByError = {
    not_found: 404,
    expired: 410,
    ended: 409,
    already_ended: 409,
    already_claimed: 409,
    state_conflict: 409
  };

  return response
    .status(
      statusByError[result.error] ?? 400
    )
    .json(result);
}

app.get(
  '/api/health',
  (_request, response) => {
    response.status(200).json({
      status: 'ok',
      service:
        'scottibyte-assist-server',
      version: '0.8.0',
      protocolVersion: 4,
      websocket: websocketStats(),
      timestamp: new Date().toISOString()
    });
  }
);

app.get(
  '/api/providers',
  requireMasterSupporter,
  (_request, response) => {
    response.status(200).json({
      providers:
        listProviderCredentials()
    });
  }
);

app.post(
  '/api/providers',
  requireMasterSupporter,
  (request, response) => {
    const result =
      createProviderCredential(
        request.body?.displayName
      );

    if (result.error) {
      return response
        .status(400)
        .json(result);
    }

    response.status(201).json(result);
  }
);

app.post(
  '/api/providers/:id/revoke',
  requireMasterSupporter,
  (request, response) => {
    const result =
      revokeProviderCredential(
        request.params.id
      );

    if (result.error) {
      return response
        .status(404)
        .json(result);
    }

    response.status(200).json(result);
  }
);

app.post(
  '/api/sessions',
  (request, response) => {
    const customerDeviceId =
      typeof request.body?.customerDeviceId
        === 'string'
        ? request.body.customerDeviceId
            .trim()
            .slice(0, 128)
        : null;

    try {
      const result =
        createCustomerSession({
          customerDeviceId:
            customerDeviceId || null,
          sourceIp: sourceIp(request)
        });

      broadcastSessionEvent(
        'session.created',
        result.session
      );

      response.status(201).json(result);
    } catch (error) {
      console.error(
        'Unable to create session:',
        error
      );

      response.status(500).json({
        error:
          'session_creation_failed',
        message:
          'Unable to create the support session.'
      });
    }
  }
);

app.get(
  '/api/sessions/:code',
  (request, response) => {
    const { code } = request.params;

    if (!validCode(code)) {
      return response.status(400).json({
        error: 'invalid_code',
        message:
          'Support codes must contain exactly six digits.'
      });
    }

    const session = getSession(code);

    if (!session) {
      return response.status(404).json({
        error: 'not_found',
        message:
          'The support code is not valid.'
      });
    }

    response.status(200).json({
      session
    });
  }
);

app.get(
  '/api/sessions/:code/receipt',
  (request, response) => {
    const { code } = request.params;

    if (!validCode(code)) {
      return response.status(400).json({
        error: 'invalid_code',
        message:
          'Support codes must contain exactly six digits.'
      });
    }

    if (
      !validateReceiptToken(
        code,
        receiptToken(request)
      )
    ) {
      return response.status(403).json({
        error:
          'receipt_authorization_required',
        message:
          'A valid receipt credential is required.'
      });
    }

    const session = getSession(code);

    if (!session) {
      return response.status(404).json({
        error: 'not_found',
        message:
          'The support session was not found.'
      });
    }

    const auditEvents =
      getAuditEventsBySessionId(
        session.id
      );

    const verification =
      verifyAuditChain(
        session.id
      );

    response.status(200).json({
      receipt: {
        generatedAt:
          new Date().toISOString(),
        session,
        auditEvents:
          auditEvents.map(
            receiptAuditEvent
          ),
        redactions: [
          'metadata.sourceIp',
          'metadata.clientId'
        ],
        verificationScope:
          'complete_server_audit_record',
        verification
      }
    });
  }
);

app.post(
  '/api/sessions/:code/claim',
  requireSupporter,
  (request, response) => {
    const { code } = request.params;

    if (!validCode(code)) {
      return response.status(400).json({
        error: 'invalid_code',
        message:
          'Support codes must contain exactly six digits.'
      });
    }

    const supporterDeviceId =
      typeof request.body?.supporterDeviceId
        === 'string'
        ? request.body.supporterDeviceId
            .trim()
            .slice(0, 128)
        : null;

    const result = claimSession(code, {
      supporterDeviceId:
        supporterDeviceId || null,
      sourceIp: sourceIp(request)
    });

    if (result.error) {
      return sendSessionError(
        response,
        result
      );
    }

    broadcastSessionEvent(
      'session.supporter_joined',
      result.session
    );

    response.status(200).json(result);
  }
);

app.post(
  '/api/sessions/:code/end',
  (request, response) => {
    const { code } = request.params;

    if (!validCode(code)) {
      return response.status(400).json({
        error: 'invalid_code',
        message:
          'Support codes must contain exactly six digits.'
      });
    }

    const supporterAuthorized =
      validateSupporterToken(
        bearerToken(request)
      );

    const customerAuthorized =
      validateCustomerToken(
        code,
        customerToken(request)
      );

    if (
      !supporterAuthorized &&
      !customerAuthorized
    ) {
      return response.status(403).json({
        error: 'end_authorization_required',
        message:
          'A valid supporter or customer credential is required.'
      });
    }

    const sessionBeforeEnd =
      getSession(code);

    const actorRole =
      supporterAuthorized
        ? 'supporter'
        : 'customer';

    const actorId =
      actorRole === 'supporter'
        ? sessionBeforeEnd
            ?.supporterDeviceId ?? null
        : sessionBeforeEnd
            ?.customerDeviceId ?? null;

    const reason =
      typeof request.body?.reason ===
        'string'
        ? request.body.reason
            .trim()
            .slice(0, 128) ||
          'user_ended'
        : 'user_ended';

    const result = endSession(code, {
      actorRole,
      actorId,
      reason,
      sourceIp: sourceIp(request)
    });

    if (result.error) {
      return sendSessionError(
        response,
        result
      );
    }

    broadcastSessionEvent(
      'session.ended',
      result.session
    );

    response.status(200).json(result);
  }
);

app.use((request, response) => {
  response.status(404).json({
    error: 'not_found',
    message:
      `No route exists for ${request.method} ${request.originalUrl}`
  });
});

app.use((
  error,
  _request,
  response,
  _next
) => {
  console.error(
    'Unhandled request error:',
    error
  );

  response.status(500).json({
    error:
      'internal_server_error',
    message:
      'An unexpected server error occurred.'
  });
});

const httpServer = http.createServer(app);

createWebSocketServer(httpServer);

httpServer.listen(
  port,
  host,
  () => {
    console.log(
      `ScottiBYTE Assist Server v0.8.0 listening on http://${host}:${port}`
    );
  }
);
