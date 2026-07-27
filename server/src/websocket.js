import {
  WebSocket,
  WebSocketServer
} from 'ws';

import {
  supporterAuthConfigured,
  validateSupporterToken
} from './auth.js';

import {
  getSession,
  validateCustomerToken
} from './sessions.js';

import {
  appendAuditEvent
} from './database.js';

const clients = new Set();

const SIGNAL_TYPES = new Set([
  'session.identity',
  'session.offer',
  'session.answer',
  'session.candidate',
  'session.video-destination',
  'session.state'
]);

function sendJson(socket, payload) {
  if (
    socket.readyState !==
    WebSocket.OPEN
  ) {
    return;
  }

  socket.send(JSON.stringify(payload));
}

function sendError(
  socket,
  error,
  message,
  requestId = null
) {
  sendJson(socket, {
    type: 'error',
    error,
    message,
    requestId
  });
}

function validCode(code) {
  return (
    typeof code === 'string' &&
    /^\d{6}$/.test(code)
  );
}

function customerSafeSession(session) {
  return {
    code: session.code,
    status: session.status,
    supporterDeviceId:
      session.supporterDeviceId,
    expiresAt: session.expiresAt,
    joinedAt: session.joinedAt,
    endedAt: session.endedAt
  };
}

function publicClientState(client) {
  return {
    id: client.id,
    role: client.role,
    authenticated:
      client.authenticated,
    sessionCode:
      client.sessionCode
  };
}

function normalizedDeviceId(value) {
  if (typeof value !== 'string') {
    return null;
  }

  const normalized =
    value.trim().slice(0, 128);

  return normalized || null;
}

function websocketSourceIp(request) {
  const forwardedFor =
    request.headers[
      'x-forwarded-for'
    ];

  if (
    typeof forwardedFor === 'string'
  ) {
    return forwardedFor
      .split(',')[0]
      .trim()
      .slice(0, 128) || null;
  }

  return request.socket
    .remoteAddress ?? null;
}

function handleSupporterAuthentication(
  client,
  message
) {
  if (!supporterAuthConfigured()) {
    return sendError(
      client.socket,
      'supporter_auth_unavailable',
      'Supporter authentication is not configured.',
      message.requestId
    );
  }

  if (
    typeof message.token !== 'string' ||
    !validateSupporterToken(
      message.token
    )
  ) {
    return sendError(
      client.socket,
      'invalid_supporter_token',
      'The supporter token is not valid.',
      message.requestId
    );
  }

  client.role = 'supporter';
  client.authenticated = true;

  sendJson(client.socket, {
    type:
      'auth.supporter.accepted',
    requestId: message.requestId,
    client:
      publicClientState(client)
  });
}

function handleSessionSubscription(
  client,
  message
) {
  const code = message.code;

  if (!validCode(code)) {
    return sendError(
      client.socket,
      'invalid_code',
      'Support codes must contain exactly six digits.',
      message.requestId
    );
  }

  const session = getSession(code);

  if (!session) {
    return sendError(
      client.socket,
      'not_found',
      'The support session was not found.',
      message.requestId
    );
  }

  if (
    session.status === 'ENDED' ||
    session.status === 'EXPIRED'
  ) {
    return sendError(
      client.socket,
      'session_unavailable',
      'The support session is no longer available.',
      message.requestId
    );
  }

  const requestedRole =
    message.role === 'supporter'
      ? 'supporter'
      : 'customer';

  if (
    requestedRole === 'supporter'
  ) {
    if (
      !client.authenticated ||
      client.role !== 'supporter'
    ) {
      return sendError(
        client.socket,
        'authorization_required',
        'Supporter authentication is required.',
        message.requestId
      );
    }

    if (
      session.status !==
      'SUPPORTER_JOINED'
    ) {
      return sendError(
        client.socket,
        'session_not_claimed',
        'Claim the support session before subscribing.',
        message.requestId
      );
    }
  } else {
    if (
      typeof message.customerToken !==
        'string' ||
      !validateCustomerToken(
        code,
        message.customerToken
      )
    ) {
      return sendError(
        client.socket,
        'invalid_customer_token',
        'The customer session token is not valid.',
        message.requestId
      );
    }

    client.authenticated = true;
  }

  client.role = requestedRole;
  client.sessionCode = code;
  client.deviceId =
    normalizedDeviceId(
      message.deviceId
    );

  try {
    appendAuditEvent({
      sessionId: session.id,
      eventType:
        `${requestedRole}.subscribed`,
      actorRole: requestedRole,
      actorId: client.deviceId,
      metadata: {
        clientId: client.id,
        sourceIp:
          client.remoteAddress
      }
    });
  } catch (error) {
    console.error(
      `Unable to audit WebSocket subscription for client ${client.id}:`,
      error
    );

    client.sessionCode = null;
    client.deviceId = null;

    if (
      requestedRole === 'customer'
    ) {
      client.role = 'anonymous';
      client.authenticated = false;
    }

    return sendError(
      client.socket,
      'audit_write_failed',
      'The session subscription could not be securely recorded.',
      message.requestId
    );
  }

  sendJson(client.socket, {
    type: 'session.subscribed',
    requestId: message.requestId,
    role: client.role,
    session:
      client.role === 'supporter'
        ? session
        : customerSafeSession(
            session
          )
  });
}

function forwardSignal(
  sender,
  message
) {
  if (
    !sender.authenticated ||
    !sender.sessionCode ||
    !['supporter', 'customer']
      .includes(sender.role)
  ) {
    return sendError(
      sender.socket,
      'subscription_required',
      'Subscribe to a session before sending signaling messages.',
      message.requestId
    );
  }

  const session =
    getSession(sender.sessionCode);

  if (
    !session ||
    session.status !==
      'SUPPORTER_JOINED'
  ) {
    return sendError(
      sender.socket,
      'session_not_ready',
      'The session is not ready for peer signaling.',
      message.requestId
    );
  }

  if (
    message.payload === undefined
  ) {
    return sendError(
      sender.socket,
      'payload_required',
      'A signaling payload is required.',
      message.requestId
    );
  }

  const encodedPayload =
    JSON.stringify(message.payload);

  if (
    Buffer.byteLength(
      encodedPayload,
      'utf8'
    ) > 48 * 1024
  ) {
    return sendError(
      sender.socket,
      'payload_too_large',
      'The signaling payload exceeds 48 KiB.',
      message.requestId
    );
  }

  const recipientRole =
    sender.role === 'supporter'
      ? 'customer'
      : 'supporter';

  const recipients = [];

  for (const client of clients) {
    if (
      client === sender ||
      client.sessionCode !==
        sender.sessionCode ||
      client.role !== recipientRole ||
      !client.authenticated ||
      client.socket.readyState !==
        WebSocket.OPEN
    ) {
      continue;
    }

    recipients.push(client);
  }

  const delivered =
    recipients.length;

  if (
    message.type ===
    'session.identity'
  ) {
    try {
      appendAuditEvent({
        sessionId: session.id,
        eventType:
          'identity.published',
        actorRole: sender.role,
        actorId: sender.deviceId,
        metadata: {
          delivered
        }
      });
    } catch (error) {
      console.error(
        `Unable to audit identity publication for client ${sender.id}:`,
        error
      );

      return sendError(
        sender.socket,
        'audit_write_failed',
        'The identity publication could not be securely recorded.',
        message.requestId
      );
    }
  }

  const signalTimestamp =
    new Date().toISOString();

  for (const recipient of recipients) {
    sendJson(recipient.socket, {
      type: message.type,
      requestId:
        message.requestId ?? null,
      fromRole: sender.role,
      timestamp: signalTimestamp,
      payload: message.payload
    });
  }

  sendJson(sender.socket, {
    type: 'session.signal.accepted',
    requestId:
      message.requestId ?? null,
    signalType: message.type,
    delivered
  });
}

function handlePing(
  client,
  message
) {
  sendJson(client.socket, {
    type: 'pong',
    requestId: message.requestId,
    timestamp:
      new Date().toISOString()
  });
}

function handleMessage(
  client,
  data
) {
  let message;

  try {
    message =
      JSON.parse(data.toString());
  } catch {
    return sendError(
      client.socket,
      'invalid_json',
      'WebSocket messages must be valid JSON.'
    );
  }

  if (
    !message ||
    typeof message !== 'object' ||
    Array.isArray(message)
  ) {
    return sendError(
      client.socket,
      'invalid_message',
      'The WebSocket message must be an object.'
    );
  }

  if (
    SIGNAL_TYPES.has(message.type)
  ) {
    return forwardSignal(
      client,
      message
    );
  }

  switch (message.type) {
    case 'auth.supporter':
      return handleSupporterAuthentication(
        client,
        message
      );

    case 'session.subscribe':
      return handleSessionSubscription(
        client,
        message
      );

    case 'ping':
      return handlePing(
        client,
        message
      );

    default:
      return sendError(
        client.socket,
        'unsupported_message',
        `Unsupported message type: ${
          message.type ?? 'undefined'
        }`,
        message.requestId
      );
  }
}

export function createWebSocketServer(
  httpServer
) {
  const webSocketServer =
    new WebSocketServer({
      server: httpServer,
      path: '/ws',
      maxPayload: 64 * 1024
    });

  let nextClientId = 1;

  webSocketServer.on(
    'connection',
    (socket, request) => {
      const client = {
        id: nextClientId,
        socket,
        role: 'anonymous',
        authenticated: false,
        sessionCode: null,
        deviceId: null,
        alive: true,
        remoteAddress:
          websocketSourceIp(request)
      };

      nextClientId += 1;
      clients.add(client);

      socket.on('pong', () => {
        client.alive = true;
      });

      socket.on(
        'message',
        (data) => {
          handleMessage(
            client,
            data
          );
        }
      );

      socket.on('close', () => {
        clients.delete(client);
      });

      socket.on(
        'error',
        (error) => {
          console.error(
            `WebSocket client ${client.id} error:`,
            error.message
          );
        }
      );

      sendJson(socket, {
        type: 'connection.ready',
        clientId: client.id,
        protocolVersion: 4,
        timestamp:
          new Date().toISOString()
      });
    }
  );

  const heartbeatInterval =
    setInterval(() => {
      for (const client of clients) {
        if (!client.alive) {
          client.socket.terminate();
          clients.delete(client);
          continue;
        }

        client.alive = false;
        client.socket.ping();
      }
    }, 30_000);

  webSocketServer.on(
    'close',
    () => {
      clearInterval(
        heartbeatInterval
      );
    }
  );

  console.log(
    'ScottiBYTE Assist WebSocket signaling available at /ws'
  );

  return webSocketServer;
}

export function broadcastSessionEvent(
  eventType,
  session
) {
  for (const client of clients) {
    if (
      client.sessionCode !==
        session.code ||
      client.socket.readyState !==
        WebSocket.OPEN
    ) {
      continue;
    }

    sendJson(client.socket, {
      type: eventType,
      timestamp:
        new Date().toISOString(),
      session:
        client.role === 'supporter'
          ? session
          : customerSafeSession(
              session
            )
    });

    if (
      eventType ===
      'session.ended'
    ) {
      client.sessionCode = null;
      client.authenticated =
        client.role === 'supporter';
    }
  }
}

export function websocketStats() {
  let supporters = 0;
  let customers = 0;
  let anonymous = 0;

  for (const client of clients) {
    switch (client.role) {
      case 'supporter':
        supporters += 1;
        break;

      case 'customer':
        customers += 1;
        break;

      default:
        anonymous += 1;
    }
  }

  return {
    total: clients.size,
    supporters,
    customers,
    anonymous
  };
}
