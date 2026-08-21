# ScottiBYTE Assist Session Protocol v4

## Scope

This document describes the current ScottiBYTE Assist server session protocol.

It covers:

- customer session creation
- provider authentication and session claiming
- WebSocket subscription
- signaling
- authenticated binary relay
- HTTP file transfer
- session ending
- customer audit receipts

Protocol version `4` is independent of the ScottiBYTE Assist client and server release versions.

The wire protocol continues to use the role name `supporter` in several REST and WebSocket fields for compatibility. In the current authorization model, that supporter is authenticated with an individual ScottiBYTE Assist **provider credential**.

---

## Roles

- **Customer**: the person requesting attended assistance.
- **Supporter**: the authenticated provider participating in that support session.

A provider credential may have either the `provider` or `superuser` role in persistent provider authorization. Both authenticate to the session protocol as the WebSocket role `supporter`.

---

## Session States

A support session can be in one of these states:

```text
WAITING
SUPPORTER_JOINED
ENDED
EXPIRED
```

A newly created session begins in `WAITING`.

After an authenticated provider successfully claims it, the session becomes `SUPPORTER_JOINED`.

An ended session becomes `ENDED`.

A session that exceeds its configured lifetime becomes `EXPIRED`.

---

## Session Lifecycle

1. The customer creates a session with `POST /api/sessions`.
2. The server returns:
   - a six-digit support `code`
   - an active `customerToken`
   - a separate `receiptToken`
3. The customer subscribes to `/ws` using the support code and customer token.
4. The provider authenticates with its individual provider credential.
5. The provider claims the support code with `POST /api/sessions/:code/claim`.
6. The provider subscribes to the same WebSocket session as role `supporter`.
7. The clients exchange signaling required by the native Assist transport.
8. The clients may establish one or more authenticated relay channels when required.
9. File-transfer state is coordinated over WebSocket while file contents are transferred through authenticated HTTP endpoints.
10. Either authorized participant may end the session.
11. The customer may retrieve the post-session audit receipt with the separate receipt token.

---

## Credentials

### Provider Credential

A provider client authenticates with its individual ScottiBYTE Assist provider credential.

For REST requests, the credential is sent as:

```text
Authorization: Bearer <provider-credential>
```

For WebSocket authentication, it is carried in an `auth.supporter` message.

The wire protocol and some server error names still use the historical term `supporter`, but the accepted credential is the current per-provider credential.

### Customer Token

The active customer token is generated when the customer creates the support session.

It authorizes customer operations associated with that session, including WebSocket subscription and customer-authorized session operations.

For HTTP requests that require it, the token is sent as:

```text
X-Customer-Token: <customer-token>
```

The active customer token becomes invalid when the session ends or expires.

### Receipt Token

The receipt token is generated independently of the active customer token.

It is sent as:

```text
X-Receipt-Token: <receipt-token>
```

It authorizes retrieval of the customer audit receipt and remains usable after the active session has ended.

Only cryptographic hashes of persistent session credentials are stored by the server.

---

# REST Protocol

## Create Customer Session

```text
POST /api/sessions
```

Example request:

```json
{
  "customerDeviceId": "Customer Computer"
}
```

Example response:

```json
{
  "session": {
    "code": "123456",
    "status": "WAITING"
  },
  "customerToken": "<customer-token>",
  "receiptToken": "<receipt-token>"
}
```

The customer should retain both returned credentials locally.

The `receiptToken` should not be shared with the provider.

---

## Get Session

```text
GET /api/sessions/:code
```

The support code must contain exactly six digits.

The endpoint returns the current session record when the code exists.

---

## Claim Session as Provider

```text
POST /api/sessions/:code/claim
```

Requires:

```text
Authorization: Bearer <provider-credential>
```

Example request:

```json
{
  "supporterDeviceId": "Support Computer"
}
```

A successful claim changes the session from `WAITING` to `SUPPORTER_JOINED`.

A session cannot be successfully claimed a second time.

The server broadcasts a `session.supporter_joined` event to WebSocket clients already associated with the session.

---

## End Session

```text
POST /api/sessions/:code/end
```

The request must be authorized by either the provider or the customer.

Provider authorization:

```text
Authorization: Bearer <provider-credential>
```

Customer authorization:

```text
X-Customer-Token: <customer-token>
```

An optional request body may include a reason:

```json
{
  "reason": "user_ended"
}
```

When the session ends, its active customer credential is invalidated and subscribed WebSocket clients receive a session-ended notification.

---

## Retrieve Customer Audit Receipt

```text
GET /api/sessions/:code/receipt
```

Requires:

```text
X-Receipt-Token: <receipt-token>
```

The response contains the session, customer-visible audit events, declared redactions, the verification scope, and the server-side audit-chain verification result.

The current verification scope is:

```text
complete_server_audit_record
```

The receipt is a redacted customer representation of an audit chain that the server verifies against its complete stored records.

---

# File Transfer REST Protocol

File transfer is available only while the support session is connected in `SUPPORTER_JOINED`.

The WebSocket protocol coordinates transfer state, while these HTTP endpoints carry transfer metadata and file content.

A participant is authenticated as either:

- the provider through `Authorization: Bearer <provider-credential>`
- the customer through `X-Customer-Token: <customer-token>`

---

## Create Transfer

```text
POST /api/sessions/:code/transfers
```

Example request:

```json
{
  "fileName": "example.zip",
  "size": 1048576
}
```

The server determines the recipient from the authenticated sender role.

A newly created transfer begins in:

```text
WAITING_UPLOAD
```

The returned transfer metadata includes fields such as:

- transfer ID
- session code
- sender role
- recipient role
- sanitized file name
- declared size
- stored size
- SHA-256 digest
- transfer status
- creation time

---

## Upload Transfer Content

```text
PUT /api/sessions/:code/transfers/:id/content
```

Only the transfer sender may upload the file.

The HTTP request body contains the raw file bytes.

During upload the transfer enters:

```text
UPLOADING
```

The server:

- counts the uploaded bytes
- calculates SHA-256 over the received bytes
- verifies the uploaded byte count against the declared size

If the byte count does not match, the partial file is removed and the transfer returns to `WAITING_UPLOAD`.

A successful upload changes the transfer to:

```text
READY
```

---

## Download Transfer Content

```text
GET /api/sessions/:code/transfers/:id/content
```

Only the transfer recipient may download the file.

The transfer must be in `READY` state.

The response uses:

```text
Content-Type: application/octet-stream
```

and includes:

```text
Content-Length: <stored-size>
X-Assist-SHA256: <sha256>
Content-Disposition: attachment; ...
```

The recipient can use `X-Assist-SHA256` to compare the downloaded content with the digest calculated by the server during upload.

---

## Delete Transfer

```text
DELETE /api/sessions/:code/transfers/:id
```

Either participant in the transfer may request deletion.

Deletion removes both the in-memory transfer record and its temporary server-side file.

---

# WebSocket Protocol

## Endpoint

```text
/ws
```

The current protocol version is:

```text
4
```

The WebSocket server accepts JSON control/signaling messages and binary relay frames.

The server WebSocket maximum payload is 64 KiB.

Signaling payload bodies are additionally limited to 48 KiB.

Binary relay chunks are limited to 48 KiB.

---

## Connection Ready

Immediately after a WebSocket connection is established, the server sends:

```json
{
  "type": "connection.ready",
  "clientId": 1,
  "protocolVersion": 4,
  "timestamp": "ISO-8601 timestamp"
}
```

The `protocolVersion` value is the authoritative session protocol version advertised by the server.

---

## Provider WebSocket Authentication

The provider sends:

```json
{
  "type": "auth.supporter",
  "requestId": "uuid",
  "token": "<provider-credential>"
}
```

Successful response:

```json
{
  "type": "auth.supporter.accepted",
  "requestId": "uuid",
  "client": {
    "id": 1,
    "role": "supporter",
    "authenticated": true,
    "sessionCode": null
  }
}
```

Provider authentication must succeed before the provider can subscribe to a support session as `supporter`.

---

## Customer Subscription

The customer sends:

```json
{
  "type": "session.subscribe",
  "requestId": "uuid",
  "role": "customer",
  "code": "123456",
  "customerToken": "<customer-token>",
  "deviceId": "Customer Computer"
}
```

The server validates the support code and customer token.

A successful subscription returns `session.subscribed`.

---

## Provider Subscription

The provider must first:

1. authenticate with `auth.supporter`
2. claim the support code through the REST claim endpoint

It then sends:

```json
{
  "type": "session.subscribe",
  "requestId": "uuid",
  "role": "supporter",
  "code": "123456",
  "deviceId": "Support Computer"
}
```

A successful subscription returns:

```json
{
  "type": "session.subscribed",
  "requestId": "uuid",
  "role": "supporter",
  "session": {}
}
```

The server records customer and supporter subscriptions in the session audit chain.

---

## Signaling Messages

The current forwarded signaling types are:

```text
session.identity
session.offer
session.answer
session.candidate
session.video-destination
session.state
file.offer
file.accept
file.decline
file.ready
file.complete
```

Every forwarded signaling message must contain a `payload`.

Example:

```json
{
  "type": "session.offer",
  "requestId": "uuid",
  "payload": {
    "type": "offer",
    "sdp": "transient signaling data"
  }
}
```

Signaling is accepted only from an authenticated participant subscribed to a support session whose state is `SUPPORTER_JOINED`.

The server forwards the message only to authenticated participants in the opposite role that are subscribed to the same session.

The recipient receives the same signal type with:

- `requestId`
- `fromRole`
- server timestamp
- payload

The sender receives an acknowledgement:

```json
{
  "type": "session.signal.accepted",
  "requestId": "uuid",
  "signalType": "session.offer",
  "delivered": 1
}
```

Signal payload bodies are transient and are not persisted in the audit database.

Publication of `session.identity` creates an `identity.published` audit event, but the identity payload itself is not stored.

---

## File Transfer Signaling

The file-transfer coordination messages are:

```text
file.offer
file.accept
file.decline
file.ready
file.complete
```

They use the same authenticated forwarding rules as other signaling messages.

These messages coordinate transfer state between the clients.

They do **not** carry the file body itself.

File bytes are uploaded and downloaded through the authenticated HTTP transfer endpoints described earlier.

---

# Authenticated Binary Relay

## Start Relay

An authenticated and subscribed participant starts a relay channel with:

```json
{
  "type": "session.relay.start",
  "requestId": "uuid",
  "channel": "main"
}
```

Supported channels are:

```text
main
voice
desktop-audio
```

If `channel` is omitted, the server defaults to `main`.

The session must be in `SUPPORTER_JOINED`.

The server responds:

```json
{
  "type": "session.relay.accepted",
  "requestId": "uuid",
  "ready": false
}
```

The `ready` value is true when a corresponding authenticated peer is already ready on the same relay channel.

When matching customer and supporter peers are ready, both receive:

```json
{
  "type": "session.relay.ready",
  "timestamp": "ISO-8601 timestamp"
}
```

---

## Relay Binary Frames

After relay readiness, binary WebSocket frames are forwarded to the authenticated participant in the opposite role when that participant:

- is subscribed to the same session
- is authenticated
- is relay-ready
- selected the same relay channel
- still has an open WebSocket

Relay frames must contain between 1 byte and 48 KiB.

The server monitors WebSocket output backlog while forwarding relay traffic.

If no eligible peer exists, the sender receives `relay_peer_unavailable`.

If a relay peer disconnects, the remaining participant receives:

```json
{
  "type": "session.relay.peer_disconnected",
  "timestamp": "ISO-8601 timestamp"
}
```

---

## Ping and Pong

A client may send:

```json
{
  "type": "ping",
  "requestId": "uuid"
}
```

The server responds:

```json
{
  "type": "pong",
  "requestId": "uuid",
  "timestamp": "ISO-8601 timestamp"
}
```

The WebSocket server also uses WebSocket-level heartbeat pings to detect unresponsive clients.

---

# Audit Behavior

The protocol records security-relevant session lifecycle events without storing the full content of the remote-assistance session.

Current audit events include:

```text
session.created
session.claimed
customer.subscribed
supporter.subscribed
identity.published
session.ended
session.expired
```

Audit records form a per-session SHA-256 hash chain.

The audit database does not persist the payload bodies of ordinary signaling messages, relay binary data, or transferred file contents.

---

# Protocol Compatibility

The current server advertises:

```text
protocolVersion: 4
```

Protocol version and product release version are independent.

A client and server should determine compatibility from the session protocol they support rather than by requiring identical client and server release numbers.

Changes to client or server product versions do not necessarily require a new protocol version.
