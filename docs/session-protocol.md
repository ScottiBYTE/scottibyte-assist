# ScottiBYTE Assist Session Protocol v3

## Roles

- **Customer**: the person requesting help.
- **Supporter**: the authenticated person providing help.

## Lifecycle

1. The customer creates a session with `POST /api/sessions`.
2. The server returns a six-digit `code` and one-time `customerToken`.
3. The customer subscribes to the WebSocket session using that token.
4. The supporter enters the customer's code and authenticates.
5. The supporter claims the session with `POST /api/sessions/:code/claim`.
6. The supporter subscribes to the WebSocket session.
7. Both peers exchange Jami identities and establish the native call.
8. Either peer may end the session with valid credentials.

## Session states

- `WAITING`
- `SUPPORTER_JOINED`
- `ENDED`
- `EXPIRED`

## REST

### Create customer session

`POST /api/sessions`

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
  "customerToken": "64-character token"
}
```

### Claim session as supporter

`POST /api/sessions/:code/claim`

Requires supporter bearer authentication.

```json
{
  "supporterDeviceId": "Support Computer"
}
```

### End session

`POST /api/sessions/:code/end`

Authorize with either:

- `Authorization: Bearer <supporter-token>`, or
- `X-Customer-Token: <customer-token>`.

## WebSocket

Protocol version: `3`

### Customer subscription

```json
{
  "type": "session.subscribe",
  "requestId": "uuid",
  "role": "customer",
  "code": "123456",
  "customerToken": "token"
}
```

### Supporter subscription

The supporter must authenticate and claim the session first.

```json
{
  "type": "session.subscribe",
  "requestId": "uuid",
  "role": "supporter",
  "code": "123456"
}
```

Signaling is allowed only while the session status is `SUPPORTER_JOINED`.
