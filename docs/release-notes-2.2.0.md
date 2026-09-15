# ScottiBYTE Assist 2.2.0

ScottiBYTE Assist 2.2.0 restores version parity between the Windows and Ubuntu Linux clients and introduces Assist Server 1.4.2.

## Highlights

- Increased WAN voice jitter buffering from 40 ms to 100 ms on both clients.
- Improved tolerance for short Internet timing variations and delayed voice-packet arrival.
- Substantially reduced WAN voice dropouts during testing.
- Preserved the dedicated voice transport, Opus encoding, in-band forward error correction, and packet-loss concealment.
- Preserved existing Assist server profiles and server-specific provider authorization.
- Retained protocol version 4 compatibility.

## Voice resilience

Windows and Ubuntu Linux now provide 100 ms of jitter buffering for voice received through the dedicated WAN relay.

Assist sends Opus voice in 20 ms packets. The previous 40 ms buffer provided approximately two packets of protection. The 100 ms buffer provides approximately five packets of protection, allowing playback to continue through a wider range of short network and scheduling delays.

Direct UDP voice buffering remains at 40 ms. No server protocol or database change is required.

## Windows

Windows 2.2.0 includes the updated 100 ms WAN voice jitter buffer.

## Ubuntu Linux

Ubuntu Linux 2.2.0 includes the same 100 ms WAN voice jitter buffer, restoring client version and voice-behavior parity with Windows.

## Server

Assist Server **1.4.2** publishes the Windows and Ubuntu Linux 2.2.0 packages and their checksums.

No database migration or protocol change is required. Existing provider registrations, Assist server profiles, and provider credentials are preserved.

Docker images:

~~~text
scottibyte/scottibyte-assist-server:1.4.2
scottibyte/scottibyte-assist-server:latest
~~~

## Client downloads

### Windows

`ScottiBYTE-Assist-Setup-2.2.0.exe`

SHA-256:

`fcbb5f6bb6637cc63631cb37ca4d4c60098a4f5d86163dfbd0b34ef082a89a2d`

### Ubuntu Linux

`ScottiBYTE-Assist_2.2.0_amd64.deb`

SHA-256:

`d106bb1a645ca5f31e778c4c96d20a5d148c2d4a0e22d14cb9c800fae68db032`

## Upgrade

Update the image reference in `docker-compose.yml` to:

~~~text
scottibyte/scottibyte-assist-server:1.4.2
~~~

Then recreate the service:

~~~bash
docker compose pull
docker compose up -d
~~~
