import http from 'node:http';
import express from 'express';

import {
  requireSupporter
} from './auth.js';

import {
  createSession,
  endSession,
  getSession,
  joinSession
} from './sessions.js';

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

app.use(express.json({
  limit: '32kb'
}));

function validCode(code) {
  return /^\d{6}$/.test(code);
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
    already_joined: 409,
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
      version: '0.5.0',
      websocket: websocketStats(),
      timestamp: new Date().toISOString()
    });
  }
);

app.post(
  '/api/sessions',
  requireSupporter,
  (request, response) => {
    const supporterDeviceId =
      typeof request.body?.supporterDeviceId
        === 'string'
        ? request.body.supporterDeviceId
            .trim()
            .slice(0, 128)
        : null;

    try {
      const session = createSession({
        supporterDeviceId:
          supporterDeviceId || null
      });

      broadcastSessionEvent(
        'session.created',
        session
      );

      response.status(201).json({
        session
      });
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

app.post(
  '/api/sessions/:code/join',
  (request, response) => {
    const { code } = request.params;

    if (!validCode(code)) {
      return response.status(400).json({
        error: 'invalid_code',
        message:
          'Support codes must contain exactly six digits.'
      });
    }

    const customerDeviceId =
      typeof request.body?.customerDeviceId
        === 'string'
        ? request.body.customerDeviceId
            .trim()
            .slice(0, 128)
        : null;

    const result = joinSession(code, {
      customerDeviceId:
        customerDeviceId || null
    });

    if (result.error) {
      return sendSessionError(
        response,
        result
      );
    }

    broadcastSessionEvent(
      'session.customer_joined',
      result.session
    );

    response.status(200).json(result);
  }
);

app.post(
  '/api/sessions/:code/end',
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

    const result = endSession(code);

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
      `ScottiBYTE Assist Server v0.5.0 listening on http://${host}:${port}`
    );
  }
);
