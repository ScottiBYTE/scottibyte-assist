# ScottiBYTE Assist 2.1.1

ScottiBYTE Assist 2.1.1 is a maintenance release for the Ubuntu Linux client and introduces Assist Server 1.4.1. The Windows client remains at version 2.1.0.

## Highlights

- Fixed pointer jumping when controlling a remote X11 desktop across a higher-latency connection.
- Prevented delayed remote X11 cursor positions from overwriting newer provider-side predicted positions.
- Restored remote Tab and Shift+Tab keyboard input.
- Restored shifted punctuation input, including number-row symbols and punctuation keys.
- Preserved existing Assist server profiles and server-specific provider authorization.
- Retained protocol version 4 compatibility.

## Ubuntu Linux

Ubuntu Linux 2.1.1 improves remote control of X11 customer desktops.

While the provider pointer is inside the remote desktop view, the provider's locally predicted cursor position remains authoritative. Remote cursor positions resume when the provider pointer leaves the remote view, allowing customer-side pointer movement to remain visible.

Keyboard translation now supports Tab, Shift+Tab, and shifted punctuation such as:

~~~text
!@#$%^&*()
_+{}|\:"<>?~
~~~

## Windows

The Windows client remains at version 2.1.0. No replacement Windows installer is included in this maintenance release.

## Server

Assist Server **1.4.1** publishes the Ubuntu Linux 2.1.1 package while continuing to publish the Windows 2.1.0 installer.

No database migration or protocol change is required. Existing provider registrations, Assist server profiles, and provider credentials are preserved.

Docker images:

~~~text
scottibyte/scottibyte-assist-server:1.4.1
scottibyte/scottibyte-assist-server:latest
~~~

## Client downloads

### Windows

`ScottiBYTE-Assist-Setup-2.1.0.exe`

SHA-256:

`c3797944f83b72cdc065d87818a7bf930c95a8945ee7b730240da7d7a4433a67`

### Ubuntu Linux

`ScottiBYTE-Assist_2.1.1_amd64.deb`

SHA-256:

`badae338a70ada30021b476d0d6695d22ffd16dd031c01de4402613e97778c29`

## Upgrade

Update the image reference in `docker-compose.yml` to:

~~~text
scottibyte/scottibyte-assist-server:1.4.1
~~~

Then recreate the service:

~~~bash
docker compose pull
docker compose up -d
~~~
