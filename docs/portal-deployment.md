# ScottiBYTE Assist Portal Deployment

## Overview

ScottiBYTE Assist Server includes a built-in web portal for
downloading Windows and Linux clients.

The official Docker image includes:

- The portal web interface
- Release metadata
- Windows and Linux client installers
- SHA-256 checksum files

No additional portal configuration is required for a
standard Docker deployment.

## Standard deployment

The recommended installation uses the official Docker image:

    scottibyte/scottibyte-assist-server:1.4.5

The bundled portal automatically serves the release information
and installers included with that image.

## Optional persistent portal configuration

Administrators who want to update portal downloads and release
metadata independently of Docker image updates can use bind mounts.

Create these files and directories alongside docker-compose.yml:

    portal-release.json
    portal-downloads/
    docker-compose.override.yml

The optional override contains:

    services:
      assist-server:
        volumes:
          - ./portal-downloads:/app/public/downloads:ro
          - ./portal-release.json:/app/release.json:ro

Copy the initial release metadata from server/release.json.

Populate portal-downloads with the required client installers
and SHA-256 checksum files.

Important: Mounting portal-downloads replaces the bundled
/app/public/downloads directory inside the container.

Therefore, all installers referenced by portal-release.json
must exist in the mounted directory.

An empty portal-downloads directory will hide the installers
bundled in the Docker image.

## Updating portal releases

When using the optional override:

1. Add new installers to portal-downloads.
2. Add their SHA-256 checksum files.
3. Update portal-release.json.
4. Verify that download paths match the installed files.
5. Confirm that the portal displays the updated release.

When using the standard deployment, update the Docker image
to receive newly bundled portal releases.

## Persistent server data

The server database and persistent application state should
remain mounted at:

    ./data:/app/data

Do not remove or overwrite this directory during upgrades.

## Current release versions

- Assist Server: 1.4.5
- Ubuntu Linux Client: 2.2.3
- Windows Client: 2.2.0

GitHub releases:

https://github.com/ScottiBYTE/scottibyte-assist/releases

Docker Hub:

https://hub.docker.com/r/scottibyte/scottibyte-assist-server
