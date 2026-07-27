# ScottiBYTE Assist Session Protocol v4

## Roles

- **Customer**: the person requesting help.
- **Supporter**: the authenticated person providing help.

## Lifecycle

1. The customer creates a session with `POST /api/sessions`.
2. The server returns:
   - a six-digit `code`
   - a one-time active `customerToken`
   - a separate `receiptToken`
3. The customer subscribes to the WebSocket session using the active customer token.
4. The supporter enters the customer’s code and authenticates.
5. The supporter claims the session with `POST /api/sessions/:code/claim`.
6. The supporter subscribes to the WebSocket session.
7. Both peers exchange native Assist identity and capability information, then establish the peer connection.
8. Either authorized peer may end the session.
9. The customer may retrieve a post-session audit receipt using the receipt token.

## Session states

- `WAITING`
- `SUPPORTER_JOINED`
- `ENDED`
- `EXPIRED`

## Credentials

### Customer token

The active customer token authorizes customer subscription and session ending.

It becomes invalid when the session ends or expires.

### Receipt token

The receipt token is generated independently from the active customer token.

It authorizes retrieval of the customer audit receipt and remains valid after the active session has ended or expired.

Only token hashes are stored by the server.

## REST

### Create customer session

`POST /api/sessions`

Request:

```json
{
  "customerDeviceId": "Customer Computer"
}
```

Response:

```json
{
  "session": {
    "code": "123456",
    "status": "WAITING"
  },
  "customerToken": "64-character token",
  "receiptToken": "64-character token"
}
```

The receipt token should be retained by the customer client and should not be shared with the supporter.

### Claim session as supporter

`POST /api/sessions/:code/claim`

Requires:

```text
Authorization: Bearer <supporter-token>
```

Request:

```json
{
  "supporterDeviceId": "Support Computer"
}
```

### End session

`POST /api/sessions/:code/end`

Authorize with either:

```text
Authorization: Bearer <supporter-token>
```

or:

```text
X-Customer-Token: <customer-token>
```

Optional request:

```json
{
  "reason": "user_ended"
}
```

### Retrieve customer receipt

`GET /api/sessions/:code/receipt`

Requires:

```text
X-Receipt-Token: <receipt-token>
```

Response outline:

```json
{
  "receipt": {
    "generatedAt": "2026-07-19T22:43:20.256Z",
    "session": {
      "code": "123456",
      "status": "ENDED"
    },
    "auditEvents": [
      {
        "sequence": 1,
        "eventType": "session.created",
        "actorRole": "customer",
        "metadata": {},
        "previousHash": null,
        "eventHash": "sha256-hash"
      }
    ],
    "redactions": [
      "metadata.sourceIp",
      "metadata.clientId"
    ],
    "verificationScope": "complete_server_audit_record",
    "verification": {
      "valid": true,
      "eventCount": 1,
      "finalHash": "sha256-hash"
    }
  }
}
```

The verification result applies to the complete server audit record. Private server metadata removed from the customer receipt may have contributed to the stored event hashes.

## WebSocket

Endpoint:

```text
/ws
```

Protocol version: `4`

### Connection ready

```json
{
  "type": "connection.ready",
  "clientId": 1,
  "protocolVersion": 3,
  "timestamp": "ISO-8601 timestamp"
}
```

### Supporter authentication

```json
{
  "type": "auth.supporter",
  "requestId": "uuid",
  "token": "supporter token"
}
```

Successful response:

```json
{
  "type": "auth.supporter.accepted",
  "requestId": "uuid"
}
```

### Customer subscription

```json
{
  "type": "session.subscribe",
  "requestId": "uuid",
  "role": "customer",
  "code": "123456",
  "customerToken": "token",
  "deviceId": "Customer Computer"
}
```

### Supporter subscription

The supporter must authenticate and claim the session first.

```json
{
  "type": "session.subscribe",
  "requestId": "uuid",
  "role": "supporter",
  "code": "123456",
  "deviceId": "Support Computer"
}
```

### Signaling types

Supported signaling message types are:

- `session.identity`
- `session.offer`
- `session.answer`
- `session.candidate`
- `session.video-destination`
- `session.state`

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

Successful sender acknowledgement:

```json
{
  "type": "session.signal.accepted",
  "requestId": "uuid",
  "signalType": "session.offer",
  "delivered": 1
}
```

Signaling is allowed only while the session status is `SUPPORTER_JOINED`.

Signaling payload contents are forwarded transiently and are not stored in the session audit database.

Identity publication creates an `identity.published` audit event, but the identity payload itself is not recorded.
