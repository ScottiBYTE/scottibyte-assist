# ScottiBYTE Assist

**ScottiBYTE Assist** is an open-source, self-hosted **attended remote-assistance platform** for Windows and Ubuntu Linux.

It is designed as an alternative to TeamViewer, Chrome Remote Desktop, AnyDesk, LogMeIn, RustDesk, and MeshCentral, with a deliberate focus on **customer-approved support sessions rather than persistent unattended access**.

![Receive Support](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/main/docs/images/receiver.png)

## Current releases

| Component | Version | Availability |
| --- | --- | --- |
| Assist Server | **1.4.4** | Docker image `scottibyte/scottibyte-assist-server:1.4.4` or `:latest` |
| Ubuntu Linux client | **2.2.2** | Ubuntu 24.04 LTS and 26.04 LTS, amd64 |
| Windows client | **2.2.0** | Windows installer |

**Linux 2.2.2** provides a unified Debian installer for Ubuntu 24.04 LTS and Ubuntu 26.04 LTS, improves Qt library compatibility, and bundles WebRTC audio-processing dependencies. Screen sharing and remote desktop control have been verified on both Ubuntu LTS releases.

The Linux and Windows clients are versioned independently of the server. The 2.2.0 voice update increased WAN voice buffering from 40 ms to 100 ms to improve audio continuity.

- [Linux 2.2.2 release](https://github.com/ScottiBYTE/scottibyte-assist/releases/tag/linux-v2.2.2)
- [Server 1.4.4 release](https://github.com/ScottiBYTE/scottibyte-assist/releases/tag/server-v1.4.4)
- [All GitHub releases](https://github.com/ScottiBYTE/scottibyte-assist/releases)

## What makes ScottiBYTE Assist different

- Customer-initiated, temporary support sessions
- Six-digit support codes instead of permanent remote-access IDs
- Explicit customer approval before desktop access
- Individually authorized and revocable provider computers
- Multiple Assist server profiles, each with separate provider authorization
- No unattended-access mode
- Self-hosted server, signaling, administration, and client download portal
- Native Windows and Ubuntu Linux clients
- X11 and Wayland support on Linux
- Remote desktop viewing and control
- Interactive Customer Terminal
- Two-way voice and text chat with distinct local and remote message colors
- Clipboard sharing and file transfer
- Provider administration and session auditing

## Public portal and client downloads

The ScottiBYTE Assist server includes a **self-hosted public portal** for information and client downloads. The Docker image includes release metadata, current Windows and Linux installers, and their SHA-256 checksum files.

![ScottiBYTE Assist Portal](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/main/docs/images/portal.png)

The portal can configure and open an installed client for its Assist server. Clients can retain multiple server profiles, with provider authorization stored separately for each server.

![ScottiBYTE Assist Settings](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/main/docs/images/settings.png)

**No separate portal mounts are required for a standard Docker deployment.** Administrators who want to maintain download files and release metadata independently of Docker image updates can use optional bind mounts as described in the [Portal Deployment Guide](https://github.com/ScottiBYTE/scottibyte-assist/blob/main/docs/portal-deployment.md).

**Important:** A downloads bind mount replaces the image's visible downloads directory. Populate it with **every installer and checksum referenced by the release metadata** before enabling that mount.

## First-time setup

On a new installation, the server generates a **one-time nine-digit setup code**. The first provider enters that code in ScottiBYTE Assist Settings to configure the server and become the initial superuser.

![First-Time Provider Setup](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/main/docs/images/bootstrap.png)

Additional providers can be authorized and revoked individually through the administrator portal.

![Provider Management](https://raw.githubusercontent.com/ScottiBYTE/scottibyte-assist/main/docs/images/admin-provider-management.png)

## Architecture

The ScottiBYTE Assist server provides authorization, session coordination, WebSocket signaling, authenticated relay services, temporary file transfer, auditing, provider management, and client downloads.

It is intended to run behind an HTTPS reverse proxy with **WebSocket support** for Internet-facing deployments.

## Docker Compose

Create a `docker-compose.yml` file:

```yaml
services:
  assist-server:
    image: scottibyte/scottibyte-assist-server:1.4.4
    container_name: scottibyte-assist-server
    restart: unless-stopped

    environment:
      NODE_ENV: production
      HOST: 0.0.0.0
      PORT: 3089
      PUBLIC_URL: https://assist.example.com
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

Replace `https://assist.example.com` with the public HTTPS URL for your server. You can substitute `:latest` for `:1.4.4` if you prefer to follow the latest published server image.

Start the server:

```bash
docker compose up -d
```

The application listens on **TCP 3089**. For Internet-facing installations, configure an HTTPS reverse proxy with WebSocket support rather than forwarding port 3089 directly from the Internet.

The `./data:/app/data` mount persists the server database. Keep that directory backed up.

## Important URLs

| Purpose | URL |
| --- | --- |
| Public portal and client server URL | `https://assist.example.com` |
| Administrator portal | `https://assist.example.com/admin` |

Replace the example domain with your own public HTTPS hostname.

## Documentation

- [Installation guide](https://github.com/ScottiBYTE/scottibyte-assist/blob/main/docs/installation.md)
- [Portal deployment guide](https://github.com/ScottiBYTE/scottibyte-assist/blob/main/docs/portal-deployment.md)
- [Architecture](https://github.com/ScottiBYTE/scottibyte-assist/blob/main/docs/architecture.md)
- [Session protocol](https://github.com/ScottiBYTE/scottibyte-assist/blob/main/docs/session-protocol.md)
- [GitHub releases](https://github.com/ScottiBYTE/scottibyte-assist/releases)
- [Source repository](https://github.com/ScottiBYTE/scottibyte-assist)

## Project

ScottiBYTE Assist is developed by **ScottiBYTE** as an open-source, self-hosted remote-support solution.

Source code, documentation, and releases: https://github.com/ScottiBYTE/scottibyte-assist
