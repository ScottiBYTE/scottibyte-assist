import http from 'node:http';
import {
  randomBytes
} from 'node:crypto';
import express from 'express';

import {
  bootstrapStatus,
  initializeBootstrap,
  redeemBootstrap
} from './bootstrap.js';

import {
  adminPasswordConfigured,
  verifyAdminPassword
} from './admin_auth.js';

import {
  requireMasterSupporter,
  requireSuperuser,
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
  renameProviderCredential,
  revokeProviderCredential
} from './providers.js';

import {
  cancelProviderEnrollment,
  createProviderEnrollment,
  listProviderEnrollments,
  redeemProviderEnrollment
} from './provider_enrollments.js';

import {
  getAuditEventsBySessionId,
  verifyAuditChain
} from './database.js';

import {
  broadcastSessionEvent,
  createWebSocketServer,
  websocketStats
} from './websocket.js';

import {
  createTransfer,
  createTransferReadStream,
  deleteTransfer,
  getTransfer,
  receiveTransferBody,
  transferView
} from './transfers.js';

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

app.use(
  express.static(
    'public',
    {
      index: 'index.html',
      extensions: ['html']
    }
  )
);

const adminSessions = new Map();

const adminSessionLifetimeMs =
  4 * 60 * 60 * 1000;

const adminCookieName =
  'assist_admin_session';

function parseCookies(request) {
  const result = {};

  for (
    const part
    of (request.headers.cookie ?? '')
      .split(';')
  ) {
    const index = part.indexOf('=');

    if (index < 0) {
      continue;
    }

    const name =
      part.slice(0, index).trim();

    const value =
      part.slice(index + 1).trim();

    if (name) {
      result[name] =
        decodeURIComponent(value);
    }
  }

  return result;
}

function createAdminSession() {
  const token =
    randomBytes(32)
      .toString('base64url');

  adminSessions.set(
    token,
    Date.now() +
      adminSessionLifetimeMs
  );

  return token;
}

function validAdminSession(request) {
  const token =
    parseCookies(request)[
      adminCookieName
    ];

  if (!token) {
    return false;
  }

  const expiresAt =
    adminSessions.get(token);

  if (
    !expiresAt ||
    expiresAt <= Date.now()
  ) {
    adminSessions.delete(token);
    return false;
  }

  return true;
}

function clearAdminSession(request) {
  const token =
    parseCookies(request)[
      adminCookieName
    ];

  if (token) {
    adminSessions.delete(token);
  }
}

function adminCookie(
  token,
  secure
) {
  return (
    `${adminCookieName}=${token}; ` +
    'Path=/; HttpOnly; ' +
    (secure ? 'Secure; ' : '') +
    'SameSite=Strict; Max-Age=14400'
  );
}

function expiredAdminCookie(
  secure
) {
  return (
    `${adminCookieName}=; ` +
    'Path=/; HttpOnly; ' +
    (secure ? 'Secure; ' : '') +
    'SameSite=Strict; Max-Age=0'
  );
}

function requireProviderAdministrator(
  request,
  response,
  next
) {
  if (validAdminSession(request)) {
    request.supporter = {
      authenticated: true,
      type: 'web-admin',
      role: 'superuser',
      providerId: null,
      displayName:
        'Assist Server Administrator'
    };

    next();
    return;
  }

  requireSuperuser(
    request,
    response,
    next
  );
}

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

function transferActorRole(
  request,
  code
) {
  if (
    validateCustomerToken(
      code,
      customerToken(request)
    )
  ) {
    return 'customer';
  }

  if (
    validateSupporterToken(
      bearerToken(request)
    )
  ) {
    return 'supporter';
  }

  return null;
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

let bootstrapFailureState = {
  startedAt: 0,
  failures: 0
};

const bootstrapFailureWindowMs =
  10 * 60 * 1000;

const bootstrapFailureLimit = 5;

function currentBootstrapFailureState() {
  const now = Date.now();

  if (
    bootstrapFailureState.startedAt === 0 ||
    now - bootstrapFailureState.startedAt >=
      bootstrapFailureWindowMs
  ) {
    bootstrapFailureState = {
      startedAt: now,
      failures: 0
    };
  }

  return bootstrapFailureState;
}

function bootstrapRateLimited() {
  return (
    currentBootstrapFailureState()
      .failures >=
    bootstrapFailureLimit
  );
}

function recordBootstrapFailure() {
  currentBootstrapFailureState()
    .failures += 1;
}

function clearBootstrapFailures() {
  bootstrapFailureState = {
    startedAt: 0,
    failures: 0
  };
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
      version: '1.2.0',
      protocolVersion: 4,
      websocket: websocketStats(),
      timestamp: new Date().toISOString()
    });
  }
);

app.get(
  '/api/admin/session',
  (request, response) => {
    response.status(200).json({
      authenticated:
        validAdminSession(request),
      passwordConfigured:
        adminPasswordConfigured()
    });
  }
);

app.post(
  '/api/admin/login',
  (request, response) => {
    if (!adminPasswordConfigured()) {
      return response.status(503).json({
        error:
          'admin_password_not_configured',
        message:
          'The administrator password has not been configured.'
      });
    }

    if (
      !verifyAdminPassword(
        request.body?.password
      )
    ) {
      return response.status(403).json({
        error:
          'invalid_admin_password',
        message:
          'The administrator password is not valid.'
      });
    }

    const token =
      createAdminSession();

    response.setHeader(
      'Set-Cookie',
      adminCookie(
        token,
        request.secure
      )
    );

    response.status(200).json({
      authenticated: true
    });
  }
);

app.post(
  '/api/admin/logout',
  (request, response) => {
    clearAdminSession(request);

    response.setHeader(
      'Set-Cookie',
      expiredAdminCookie(
        request.secure
      )
    );

    response.status(200).json({
      authenticated: false
    });
  }
);

app.get(
  '/api/bootstrap/status',
  (_request, response) => {
    response.status(200).json(
      bootstrapStatus()
    );
  }
);

app.post(
  '/api/bootstrap/redeem',
  (request, response) => {
    if (bootstrapRateLimited()) {
      return response
        .status(429)
        .json({
          error:
            'bootstrap_rate_limited',
          message:
            'Too many invalid setup attempts. '
            + 'Try again later.'
        });
    }

    const result =
      redeemBootstrap(
        request.body?.setupCode,
        request.body?.displayName,
        request.body?.adminPassword
      );

    if (result.error) {
      if (
        result.error ===
        'invalid_setup_code'
      ) {
        recordBootstrapFailure();
      }

      const status =
        result.error ===
          'bootstrap_not_available'
          ? 409
          : 403;

      return response
        .status(status)
        .json(result);
    }

    clearBootstrapFailures();

    response.status(201).json(result);
  }
);

app.get(
  '/api/provider-enrollments',
  requireProviderAdministrator,
  (_request, response) => {
    response.status(200).json({
      enrollments:
        listProviderEnrollments()
    });
  }
);

app.post(
  '/api/provider-enrollments',
  requireProviderAdministrator,
  (request, response) => {
    const result =
      createProviderEnrollment(
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
  '/api/provider-enrollments/:id/cancel',
  requireProviderAdministrator,
  (request, response) => {
    const result =
      cancelProviderEnrollment(
        request.params.id
      );

    if (result.error) {
      return response
        .status(409)
        .json(result);
    }

    response.status(200).json(result);
  }
);

app.post(
  '/api/provider-enrollments/redeem',
  (request, response) => {
    const result =
      redeemProviderEnrollment(
        request.body?.enrollmentCode
      );

    if (result.error) {
      const status =
        result.error ===
          'invalid_enrollment_code'
          ? 403
          : 409;

      return response
        .status(status)
        .json(result);
    }

    response.status(201).json(result);
  }
);

app.get(
  '/api/providers',
  requireProviderAdministrator,
  (_request, response) => {
    response.status(200).json({
      providers:
        listProviderCredentials()
    });
  }
);

app.post(
  '/api/providers',
  requireProviderAdministrator,
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

app.patch(
  '/api/providers/:id',
  requireProviderAdministrator,
  (request, response) => {
    const result =
      renameProviderCredential(
        request.params.id,
        request.body?.displayName
      );

    if (result.error) {
      const status =
        result.error === 'provider_not_found'
          ? 404
          : 400;

      return response
        .status(status)
        .json(result);
    }

    response.status(200).json(result);
  }
);

app.post(
  '/api/providers/:id/revoke',
  requireProviderAdministrator,
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
  '/api/sessions/:code/transfers',
  async (request, response) => {
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
          'The support session was not found.'
      });
    }

    if (
      session.status !==
      'SUPPORTER_JOINED'
    ) {
      return response.status(409).json({
        error: 'session_not_connected',
        message:
          'File transfer requires a connected support session.'
      });
    }

    const senderRole =
      transferActorRole(
        request,
        code
      );

    if (!senderRole) {
      return response.status(403).json({
        error:
          'transfer_authorization_required',
        message:
          'A valid session credential is required.'
      });
    }

    const result =
      await createTransfer({
        sessionCode: code,
        senderRole,
        fileName:
          request.body?.fileName,
        declaredSize:
          request.body?.size
      });

    if (result.error) {
      return response
        .status(400)
        .json(result);
    }

    response.status(201).json(result);
  }
);

app.put(
  '/api/sessions/:code/transfers/:id/content',
  async (request, response) => {
    const { code, id } =
      request.params;

    const transfer =
      getTransfer(code, id);

    if (!transfer) {
      return response.status(404).json({
        error: 'transfer_not_found',
        message:
          'The file transfer was not found.'
      });
    }

    const actorRole =
      transferActorRole(
        request,
        code
      );

    if (
      !actorRole ||
      actorRole !== transfer.senderRole
    ) {
      return response.status(403).json({
        error:
          'transfer_sender_required',
        message:
          'Only the transfer sender may upload this file.'
      });
    }

    const result =
      await receiveTransferBody(
        transfer,
        request
      );

    if (result.error) {
      return response
        .status(409)
        .json(result);
    }

    response.status(200).json(result);
  }
);

app.get(
  '/api/sessions/:code/transfers/:id/content',
  (request, response) => {
    const { code, id } =
      request.params;

    const transfer =
      getTransfer(code, id);

    if (!transfer) {
      return response.status(404).json({
        error: 'transfer_not_found',
        message:
          'The file transfer was not found.'
      });
    }

    const actorRole =
      transferActorRole(
        request,
        code
      );

    if (
      !actorRole ||
      actorRole !== transfer.recipientRole
    ) {
      return response.status(403).json({
        error:
          'transfer_recipient_required',
        message:
          'Only the transfer recipient may download this file.'
      });
    }

    const input =
      createTransferReadStream(
        transfer
      );

    if (!input) {
      return response.status(409).json({
        error: 'transfer_not_ready',
        message:
          'The file is not ready for download.'
      });
    }

    response.setHeader(
      'Content-Type',
      'application/octet-stream'
    );

    response.setHeader(
      'Content-Length',
      String(transfer.storedSize)
    );

    response.setHeader(
      'X-Assist-SHA256',
      transfer.sha256
    );

    response.setHeader(
      'Content-Disposition',
      `attachment; filename*=UTF-8''${
        encodeURIComponent(
          transfer.fileName
        )
      }`
    );

    input.on(
      'error',
      (error) => {
        console.error(
          'Transfer download failed:',
          error
        );

        if (!response.headersSent) {
          response.status(500).end();
        } else {
          response.destroy(error);
        }
      }
    );

    input.pipe(response);
  }
);

app.delete(
  '/api/sessions/:code/transfers/:id',
  async (request, response) => {
    const { code, id } =
      request.params;

    const transfer =
      getTransfer(code, id);

    if (!transfer) {
      return response.status(404).json({
        error: 'transfer_not_found',
        message:
          'The file transfer was not found.'
      });
    }

    const actorRole =
      transferActorRole(
        request,
        code
      );

    if (
      !actorRole ||
      (
        actorRole !== transfer.senderRole &&
        actorRole !== transfer.recipientRole
      )
    ) {
      return response.status(403).json({
        error:
          'transfer_authorization_required',
        message:
          'A valid transfer participant credential is required.'
      });
    }

    await deleteTransfer(
      transfer
    );

    response.status(200).json({
      deleted: true,
      transfer:
        transferView(transfer)
    });
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

initializeBootstrap();

httpServer.listen(
  port,
  host,
  () => {
    console.log(
      `ScottiBYTE Assist Server v1.2.0 listening on http://${host}:${port}`
    );
  }
);
