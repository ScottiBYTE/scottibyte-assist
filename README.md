# ScottiBYTE Assist

**ScottiBYTE Assist** is a self-hosted remote-support platform designed around customer-created, one-time support sessions.

The customer starts a session and receives a six-digit support code. An authenticated supporter claims that code, and both applications use the ScottiBYTE Assist server for authorization, session coordination, audit records, and signaling. Native peer communication is handled separately by Jami.

## Current Server Release: v0.7.0

`v0.7.0` adds the security and audit foundation for customer-authorized remote-support sessions.

### Added

- Hash-chained audit records for each support session.
- Atomic session creation, supporter claiming, ending, and expiration.
- Dedicated post-session receipt credentials separate from the active customer session token.
- Authenticated customer audit receipts available after a session ends or expires.
- Auditing of customer and supporter WebSocket subscriptions.
- Auditing of identity publication without storing the identity payload.
- Audit-chain verification with event count and final hash.
- Explicit receipt disclosure for fields removed from customer-visible audit metadata.

### Privacy and data minimization

ScottiBYTE Assist does not store the content of:

- Jami identity payloads
- signaling offers
- signaling answers
- ICE candidates
- screen contents
- audio
- keystrokes
- clipboard contents

The server records that an identity was published and how many recipients received it, but it does not retain the identity payload itself.

Customer receipts redact:

- `metadata.sourceIp`
- `metadata.clientId`

Receipt verification is performed against the complete server-side audit record. Because the customer-visible receipt omits those private server fields, its hash chain is reported as server-verified rather than independently recomputable from only the redacted receipt.

## Session flow

1. The customer creates a session.
2. The server returns:
   - a six-digit support code
   - an active customer session token
   - a separate receipt token
3. The customer shares only the six-digit code with the supporter.
4. The authenticated supporter claims the session.
5. The customer and supporter subscribe over WebSocket.
6. Both peers exchange Jami identities through transient signaling.
7. The native Jami connection carries the call media.
8. Either authorized peer may end the session.
9. The customer can retrieve the audit receipt with the receipt token.

## Session states

- `WAITING`
- `SUPPORTER_JOINED`
- `ENDED`
- `EXPIRED`

## Components

### Assist server

The server provides:

- REST session lifecycle endpoints
- supporter authentication
- customer token validation
- receipt-token validation
- WebSocket coordination and signaling
- SQLite session storage
- hash-chained audit storage
- customer audit receipts

### Assist clients

Customer and supporter clients are separate applications that communicate with this server over HTTPS and secure WebSocket connections.

Jami remains responsible for native peer communication and media transport.

## Deployment

The server is packaged as a Docker container.

```bash
docker compose up -d --build
```

The production deployment should expose the Assist server only through the configured reverse proxy.

### Important proxy security requirement

The application trusts one reverse-proxy hop for client address information. The server port must not be exposed directly to untrusted networks.

In the current deployment, Nginx Proxy Manager is the sole public entry point. Direct WAN access to port `3089` must remain blocked.

## Environment variables

The server uses these primary settings:

```env
HOST=0.0.0.0
PORT=3089
DATABASE_PATH=/app/data/assist.sqlite
SESSION_LIFETIME_MINUTES=30
SUPPORTER_API_TOKEN=replace-with-a-strong-secret
```

Keep the supporter token private. Customer and receipt tokens are generated independently for each session.

## API overview

### Health

```text
GET /api/health
```

### Create a customer session

```text
POST /api/sessions
```

### Claim a session

```text
POST /api/sessions/:code/claim
```

Requires supporter bearer authentication.

### End a session

```text
POST /api/sessions/:code/end
```

Requires either the supporter bearer token or active customer token.

### Retrieve a customer receipt

```text
GET /api/sessions/:code/receipt
```

Requires the dedicated receipt token:

```text
X-Receipt-Token: <receipt-token>
```

### WebSocket signaling

```text
/ws
```

The current session protocol version is `3`.

See [docs/session-protocol.md](docs/session-protocol.md) for protocol details and [docs/architecture.md](docs/architecture.md) for the system design.
