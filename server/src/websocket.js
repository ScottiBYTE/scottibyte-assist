import {
  WebSocket,
  WebSocketServer
} from 'ws';

import {
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
const supportRequests = new Map();

const RELAY_CHUNK_MAX_BYTES =
  48 * 1024;

const SIGNAL_TYPES = new Set([
  'session.identity',
  'session.offer',
  'session.answer',
  'session.candidate',
  'session.video-destination',
  'session.state',
  'file.offer',
  'file.accept',
  'file.decline',
  'file.ready',
  'file.complete'
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

function isIdleSupporter(client) {
  return (
    client.role === 'supporter' &&
    client.authenticated &&
    client.available === true &&
    client.sessionCode === null &&
    client.socket.readyState === WebSocket.OPEN
  );
}

function sendAvailableSupportRequests(client) {
  if (!isIdleSupporter(client)) {
    return;
  }

  for (const request of supportRequests.values()) {
    if (
      request.requesterDeviceId &&
      client.deviceId &&
      request.requesterDeviceId.toLowerCase() ===
        client.deviceId.toLowerCase()
    ) {
      continue;
    }

    sendJson(client.socket, {
      type: 'support.request.available',
      code: request.code,
      requesterName: request.requesterName,
      createdAt: request.createdAt
    });
  }
}

function broadcastSupportRequestClosed(
  request,
  status,
  supporterDeviceId = null
) {
  supportRequests.delete(request.code);

  const closedMessage = {
    type: 'support.request.closed',
    code: request.code,
    status
  };

  for (const client of clients) {
    if (
      client.role === 'supporter' &&
      client.authenticated &&
      client.sessionCode === null &&
      client.socket.readyState === WebSocket.OPEN
    ) {
      sendJson(client.socket, closedMessage);
    }
  }

  const requester = request.requester;

  if (
    requester?.socket.readyState === WebSocket.OPEN
  ) {
    sendJson(requester.socket, {
      type: 'support.request.state',
      code: request.code,
      status,
      supporterDeviceId
    });
  }
}

function closeSupportRequest(
  code,
  status,
  supporterDeviceId = null
) {
  const request = supportRequests.get(code);

  if (!request) {
    return false;
  }

  broadcastSupportRequestClosed(
    request,
    status,
    supporterDeviceId
  );
  return true;
}

function handleSupportRequest(client, message) {
  if (
    !client.authenticated ||
    client.role !== 'customer' ||
    !client.sessionCode
  ) {
    return sendError(
      client.socket,
      'customer_subscription_required',
      'Subscribe as the customer before requesting support.',
      message.requestId
    );
  }

  const session = getSession(client.sessionCode);

  if (!session || session.status !== 'WAITING') {
    return sendError(
      client.socket,
      'session_unavailable',
      'This support session is no longer available for a request.',
      message.requestId
    );
  }

  const existing = supportRequests.get(client.sessionCode);

  if (existing && existing.requester !== client) {
    return sendError(
      client.socket,
      'request_already_exists',
      'A support request is already active for this code.',
      message.requestId
    );
  }

  const request = existing || {
    code: client.sessionCode,
    requester: client,
    requesterDeviceId: client.deviceId,
    requesterName: (client.deviceId || 'Unknown workstation')
      .replace(/[\u0000-\u001f\u007f]/g, '')
      .slice(0, 128),
    createdAt: new Date().toISOString()
  };

  supportRequests.set(request.code, request);

  sendJson(client.socket, {
    type: 'support.request.state',
    requestId: message.requestId,
    code: request.code,
    status: 'pending'
  });

  for (const provider of clients) {
    if (!isIdleSupporter(provider)) {
      continue;
    }

    if (
      request.requesterDeviceId &&
      provider.deviceId &&
      request.requesterDeviceId.toLowerCase() ===
        provider.deviceId.toLowerCase()
    ) {
      continue;
    }

    sendJson(provider.socket, {
      type: 'support.request.available',
      code: request.code,
      requesterName: request.requesterName,
      createdAt: request.createdAt
    });
  }
}

function handleSupportRequestCancel(client, message) {
  if (
    !client.authenticated ||
    client.role !== 'customer' ||
    !client.sessionCode
  ) {
    return sendError(
      client.socket,
      'customer_subscription_required',
      'Subscribe as the customer before canceling a support request.',
      message.requestId
    );
  }

  const request = supportRequests.get(client.sessionCode);

  if (!request || request.requester !== client) {
    return sendJson(client.socket, {
      type: 'support.request.state',
      requestId: message.requestId,
      code: client.sessionCode,
      status: 'canceled'
    });
  }

  broadcastSupportRequestClosed(request, 'canceled');
}

function handleSupporterAvailable(client, message) {
  if (!client.authenticated || client.role !== 'supporter') {
    return sendError(
      client.socket,
      'authorization_required',
      'Supporter authentication is required.',
      message.requestId
    );
  }

  if (client.sessionCode) {
    return sendError(
      client.socket,
      'session_active',
      'Disconnect the active session before receiving requests.',
      message.requestId
    );
  }

  client.available = true;

  sendJson(client.socket, {
    type: 'supporter.availability.accepted',
    requestId: message.requestId
  });

  sendAvailableSupportRequests(client);
}

function handleSupporterBusy(client, message) {
  if (
    !client.authenticated ||
    client.role !== 'supporter' ||
    client.sessionCode
  ) {
    return sendError(
      client.socket,
      'authorization_required',
      'An idle authenticated supporter is required.',
      message.requestId
    );
  }

  client.available = false;
  sendJson(client.socket, {
    type: 'supporter.busy.accepted',
    requestId: message.requestId
  });
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
  client.available = false;
  client.deviceId = normalizedDeviceId(
    message.deviceId
  );

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
  if (requestedRole === 'supporter') {
    client.available = false;
  }
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

function relayPeers(
  client
) {
  const recipientRole =
    client.role === 'supporter'
      ? 'customer'
      : 'supporter';

  return [...clients].filter(
    peer =>
      peer !== client &&
      peer.sessionCode ===
        client.sessionCode &&
      peer.role === recipientRole &&
      peer.authenticated &&
      peer.relayReady &&
      peer.relayChannel ===
        client.relayChannel &&
      peer.socket.readyState ===
        WebSocket.OPEN
  );
}

function notifyRelayPeerDisconnected(
  client
) {
  if (
    !client.authenticated ||
    !client.sessionCode ||
    !client.relayReady ||
    !['supporter', 'customer']
      .includes(client.role)
  ) {
    return;
  }

  const peers =
    relayPeers(client);

  const timestamp =
    new Date().toISOString();

  for (const peer of peers) {
    peer.relayReady = false;

    sendJson(peer.socket, {
      type:
        'session.relay.peer_disconnected',
      timestamp
    });
  }
}

function handleRelayStart(
  client,
  message
) {
  if (
    !client.authenticated ||
    !client.sessionCode ||
    !['supporter', 'customer']
      .includes(client.role)
  ) {
    return sendError(
      client.socket,
      'subscription_required',
      'Subscribe to a session before starting the relay.',
      message.requestId
    );
  }

  const session =
    getSession(client.sessionCode);

  if (
    !session ||
    session.status !==
      'SUPPORTER_JOINED'
  ) {
    return sendError(
      client.socket,
      'session_not_ready',
      'The session is not ready for relay traffic.',
      message.requestId
    );
  }

  const requestedChannel =
    typeof message.channel === 'string'
      ? message.channel.trim().toLowerCase()
      : 'main';

  if (
    requestedChannel !== 'main' &&
    requestedChannel !== 'voice' &&
    requestedChannel !== 'desktop-audio'
  ) {
    return sendError(
      client.socket,
      'invalid_relay_channel',
      'The requested relay channel is not supported.',
      message.requestId
    );
  }

  client.relayChannel =
    requestedChannel;
  console.log("Relay channel selected", { clientId: client.id, role: client.role, sessionCode: client.sessionCode, deviceId: client.deviceId, channel: client.relayChannel });

  client.relayReady = true;

  const peers =
    relayPeers(client);

  sendJson(client.socket, {
    type: 'session.relay.accepted',
    requestId:
      message.requestId ?? null,
    ready:
      peers.length > 0
  });

  if (peers.length === 0) {
    return;
  }

  const timestamp =
    new Date().toISOString();

  sendJson(client.socket, {
    type: 'session.relay.ready',
    timestamp
  });

  for (const peer of peers) {
    sendJson(peer.socket, {
      type: 'session.relay.ready',
      timestamp
    });
  }
}

function handleRelayBinary(
  client,
  data
) {
  if (
    !client.authenticated ||
    !client.sessionCode ||
    !client.relayReady ||
    !['supporter', 'customer']
      .includes(client.role)
  ) {
    return sendError(
      client.socket,
      'relay_not_ready',
      'The relay is not ready for binary traffic.'
    );
  }

  const session =
    getSession(client.sessionCode);

  if (
    !session ||
    session.status !==
      'SUPPORTER_JOINED'
  ) {
    return sendError(
      client.socket,
      'session_not_ready',
      'The session is not ready for relay traffic.'
    );
  }

  const payload =
    Buffer.isBuffer(data)
      ? data
      : Buffer.from(data);

  if (
    payload.length === 0 ||
    payload.length >
      RELAY_CHUNK_MAX_BYTES
  ) {
    return sendError(
      client.socket,
      'invalid_relay_chunk',
      `Relay chunks must contain between 1 and ${
        RELAY_CHUNK_MAX_BYTES
      } bytes.`
    );
  }

  const peers =
    relayPeers(client);

  if (peers.length === 0) {
    return sendError(
      client.socket,
      'relay_peer_unavailable',
      'The relay peer is not currently available.'
    );
  }

  for (const peer of peers) {
    if (
      peer.socket.bufferedAmount >
        1024 * 1024
    ) {
      const now = Date.now();

      if (
        !peer.lastRelayBacklogLogAt ||
        now - peer.lastRelayBacklogLogAt >=
          5000
      ) {
        console.warn(
          `Relay destination backlog for WebSocket client ${peer.id}`,
          {
            role:
              peer.role,
            sessionCode:
              peer.sessionCode,
            deviceId:
              peer.deviceId,
            bufferedAmount:
              peer.socket.bufferedAmount
          }
        );

        peer.lastRelayBacklogLogAt =
          now;
      }
    }

    peer.socket.send(
      payload,
      {
        binary: true
      }
    );
  }
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

    case 'support.request.create':
      return handleSupportRequest(
        client,
        message
      );

    case 'support.request.cancel':
      return handleSupportRequestCancel(
        client,
        message
      );

    case 'supporter.available':
      return handleSupporterAvailable(
        client,
        message
      );

    case 'supporter.busy':
      return handleSupporterBusy(
        client,
        message
      );

    case 'session.relay.start':
      return handleRelayStart(
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
        available: false,
        sessionCode: null,
        deviceId: null,
        relayReady: false,
        relayChannel: 'main',
        alive: true,
        lastActivityAt: Date.now(),
        remoteAddress:
          websocketSourceIp(request)
      };

      nextClientId += 1;
      clients.add(client);

      console.log(
        `WebSocket client ${client.id} connected`,
        {
          remoteAddress:
            client.remoteAddress
        }
      );

      socket.on('pong', () => {
        client.alive = true;
        client.lastActivityAt = Date.now();
      });

      socket.on(
        'message',
        (data, isBinary) => {
          client.alive = true;
          client.lastActivityAt = Date.now();

          if (isBinary) {
            handleRelayBinary(
              client,
              data
            );
            return;
          }

          handleMessage(
            client,
            data
          );
        }
      );

      socket.on(
        'close',
        (code, reason) => {
          console.log(
            `WebSocket client ${client.id} closed`,
            {
              code,
              reason:
                reason.toString(),
              role:
                client.role,
              sessionCode:
                client.sessionCode,
              deviceId:
                client.deviceId,
              relayReady:
                client.relayReady,
              remoteAddress:
                client.remoteAddress
            }
          );

          notifyRelayPeerDisconnected(
            client
          );

          for (const request of supportRequests.values()) {
            if (request.requester === client) {
              broadcastSupportRequestClosed(
                request,
                'requester_disconnected'
              );
            }
          }

          clients.delete(client);
        }
      );

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
      const now = Date.now();
      const inactivityLimitMs = 90_000;

      for (const client of clients) {
        const inactiveForMs =
          now - client.lastActivityAt;

        if (
          !client.alive &&
          inactiveForMs >= inactivityLimitMs
        ) {
          console.warn(
            `Terminating unresponsive WebSocket client ${client.id}`,
            {
              role:
                client.role,
              sessionCode:
                client.sessionCode,
              deviceId:
                client.deviceId,
              relayReady:
                client.relayReady,
              inactiveForMs,
              remoteAddress:
                client.remoteAddress
            }
          );

          client.socket.terminate();
          continue;
        }

        client.alive = false;
        client.socket.ping();
      }

      for (const request of supportRequests.values()) {
        const session = getSession(request.code);

        if (!session || session.status !== 'WAITING') {
          broadcastSupportRequestClosed(
            request,
            session?.status === 'EXPIRED'
              ? 'expired'
              : 'session_unavailable'
          );
        }
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
  if (eventType === 'session.ended') {
    closeSupportRequest(
      session.code,
      'session_ended'
    );
  }

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
      client.relayReady = false;
      client.authenticated =
        client.role === 'supporter';
      client.available = false;
    }
  }
}

export function resolveSupportRequest(
  code,
  supporterDeviceId = null
) {
  return closeSupportRequest(
    code,
    'claimed',
    supporterDeviceId
  );
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
