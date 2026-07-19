# ScottiBYTE Assist Architecture

## Overview

ScottiBYTE Assist separates session authorization and coordination from native peer communication.

The Assist server is responsible for:

- creating one-time customer sessions
- authenticating supporters
- authorizing session claims
- validating active customer credentials
- coordinating WebSocket subscriptions
- forwarding transient signaling messages
- recording a hash-chained audit history
- issuing authenticated customer receipts

Jami is responsible for native peer communication and call media.

## Data flow

```text
Customer client
    |
    | HTTPS / WSS
    v
Nginx Proxy Manager
    |
    | private HTTP / WS
    v
ScottiBYTE Assist server
    |
    +-- SQLite session and audit database
    |
    +-- transient signaling forwarding
            |
            v
       Supporter client

Customer client <------ native Jami connection ------> Supporter client
```

The Assist server does not relay or store Jami call media.

## Trust boundaries

### Public boundary

Nginx Proxy Manager is the intended sole public entry point.

The application trusts one reverse-proxy hop. Client address information may therefore be derived from proxy headers.

Direct access to the application port from untrusted networks must be blocked. Allowing clients to bypass the reverse proxy could permit forged forwarding headers.

### Supporter authentication

Supporters authenticate with the configured bearer token before claiming or subscribing to a session.

The server currently treats possession of that configured token as supporter authorization.

### Customer authorization

Each newly created session receives two independent credentials:

- `customerToken`
- `receiptToken`

The active customer token authorizes:

- customer WebSocket subscription
- customer-initiated session ending

The active customer token is removed when the session ends or expires.

The receipt token authorizes retrieval of the post-session customer audit receipt and remains available after the active token has been invalidated.

Only cryptographic hashes of these tokens are stored in SQLite.

## Session lifecycle

### Creation

The session row and `session.created` audit event are written in one immediate SQLite transaction.

Either both records commit or neither record exists.

### Claiming

The transition from `WAITING` to `SUPPORTER_JOINED` and the `session.claimed` audit event occur in one immediate transaction.

A failed audit write leaves the session unclaimed.

### Ending

The transition to `ENDED`, removal of the active customer token hash, end timestamp, and `session.ended` audit event occur in one immediate transaction.

A failed audit write leaves the session active and preserves the customer credential.

### Expiration

Expired active sessions are changed to `EXPIRED`, their active customer token hashes are removed, and `session.expired` events are appended in one transaction.

## Audit chain

Every session has an ordered sequence of audit events.

Each event contains:

- session ID
- sequence number
- timestamp
- event type
- actor role
- actor ID
- canonical metadata JSON
- previous event hash
- current event hash

The event hash is computed with SHA-256 over the canonical event representation and previous event hash.

This creates a per-session hash chain. Changing an earlier event or its metadata causes chain verification to fail.

## Audited events

Current event types include:

- `session.created`
- `session.claimed`
- `customer.subscribed`
- `supporter.subscribed`
- `identity.published`
- `session.ended`
- `session.expired`

Identity publication records only:

- that publication occurred
- the publishing role and device
- the number of recipients

The identity payload is not stored.

## Data intentionally not stored

The Assist audit database does not store:

- Jami identity payload contents
- offer payloads
- answer payloads
- ICE candidate payloads
- screen contents
- audio
- keystrokes
- clipboard data
- signaling payload bodies

Offer, answer, and candidate messages are forwarded only to authenticated peers currently subscribed to the claimed session.

## Customer receipts

The receipt endpoint returns:

- normalized session information
- ordered customer-visible audit events
- redaction declarations
- verification scope
- server-side chain-verification result

The receipt removes these audit metadata fields:

- `sourceIp`
- `clientId`

The event hashes were generated from the complete stored audit metadata. Therefore, the redacted receipt cannot independently reproduce every event hash.

The receipt reports:

```text
verificationScope: complete_server_audit_record
```

This means the server verified the complete stored chain before returning the redacted customer representation.

## Database

SQLite runs with:

- WAL journal mode
- foreign keys enabled
- a busy timeout
- strict tables

The primary tables are:

- `sessions`
- `session_audit_events`

Audit records reference their session through a foreign key.

## Protocol compatibility

Server release v0.7.0 continues to use session protocol version `3`.

The security and audit changes do not require a protocol-version increase because the existing session and WebSocket message flow remains compatible. The create-session REST response now also includes the dedicated receipt token.
