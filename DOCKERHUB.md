# ScottiBYTE Assist

**ScottiBYTE Assist** is an open-source, self-hosted attended remote-assistance platform for Windows and Linux.

It is designed as an alternative to tools such as TeamViewer, Chrome Remote Desktop, AnyDesk, LogMeIn, RustDesk, and MeshCentral, while deliberately focusing on **attended remote assistance rather than persistent unattended remote access**.

![Receive Support](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/v1.0.2/docs/images/receiver.png)

## What makes ScottiBYTE Assist different

- Customer-initiated temporary support sessions
- Six-digit support codes rather than permanent remote-access IDs
- Customer approval before desktop access begins
- Individually authorized and revocable provider computers
- No unattended-access mode
- Self-hosted server, signaling, administration, and client portal
- Native Windows and Ubuntu Linux clients
- X11 and Wayland support on Linux
- Remote desktop viewing and control
- Two-way voice
- Text chat
- Clipboard sharing
- File transfer
- Provider administration and session auditing

## Public Portal

The ScottiBYTE Assist server includes a self-hosted client download and information portal.

![ScottiBYTE Assist Portal](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/v1.0.2/docs/images/portal.png)

## First-Time Setup

A new ScottiBYTE Assist server generates a one-time nine-digit setup code.

The first provider uses that code in ScottiBYTE Assist Settings to configure the server and become the initial superuser.

![First-Time Provider Setup](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/v1.0.2/docs/images/bootstrap.png)

Additional providers can then be authorized and revoked individually through the administrator portal.

![Provider Management](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/v1.0.2/docs/images/admin-provider-management.png)

## Architecture

The ScottiBYTE Assist server provides authorization, session coordination, WebSocket signaling, authenticated relay services, temporary file transfer, auditing, provider management, and client downloads.

![ScottiBYTE Assist Architecture](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/v1.0.2/docs/images/architecture.png)

## Docker Compose

```yaml
services:
  assist-server:
    image:
      scottibyte/scottibyte-assist-server:latest

    container_name:
      scottibyte-assist-server

    restart:
      unless-stopped

    environment:
      NODE_ENV: production
      HOST: 0.0.0.0
      PORT: 3089
      DATABASE_PATH: /app/data/assist.sqlite
      SESSION_LIFETIME_MINUTES: 30

    ports:
      - "3089:3089"

    volumes:
      - ./data:/app/data

    healthcheck:
      test:
        - CMD
        - wget
        - --quiet
        - --spider
        - http://127.0.0.1:3089/api/health
      interval: 30s
      timeout: 5s
      retries: 3
      start_period: 10s
```

Start the server with:

```bash
docker compose up -d
```

The application listens on TCP `3089`.

For Internet-facing deployments, place ScottiBYTE Assist behind an HTTPS reverse proxy with WebSocket support rather than exposing TCP 3089 directly.

## Important URLs

Public portal and client Server URL:

```text
https://assist.example.com
```

Administrator portal:

```text
https://assist.example.com/admin
```

## Documentation

- Installation: https://github.com/ScottiBYTE/scottibyte-assist/blob/v1.0.2/docs/installation.md
- Architecture: https://github.com/ScottiBYTE/scottibyte-assist/blob/v1.0.2/docs/architecture.md
- Session Protocol: https://github.com/ScottiBYTE/scottibyte-assist/blob/v1.0.2/docs/session-protocol.md
- Source: https://github.com/ScottiBYTE/scottibyte-assist

## Current Releases

The Docker image contains the ScottiBYTE Assist public portal, the Windows 1.0.2 client installer, and the Linux 1.0.2 client installer.

Server version and client release versions are independent.

## Project

ScottiBYTE Assist is developed by ScottiBYTE.

Source code and release information:

https://github.com/ScottiBYTE/scottibyte-assist
