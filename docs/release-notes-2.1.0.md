# ScottiBYTE Assist 2.1.0

ScottiBYTE Assist 2.1.0 brings the Windows and Ubuntu Linux clients to feature parity and introduces Assist Server 1.4.0.

## Highlights

- Added multiple Assist server profiles with Add, Edit, and Delete controls.
- Protected the primary Assist server profile from deletion.
- Stored provider authorization separately for each Assist server.
- Preserved provider authorization when the primary server URL is renamed.
- Reconnected immediately after changing the active Assist server.
- Added portal links that configure and open an installed ScottiBYTE Assist client.
- Added distinct local and remote chat message colors.
- Retains remote desktop, Customer Terminal, voice, chat, clipboard, file transfer, and application-sharing capabilities.

## Windows

Windows 2.1.0 adds full Assist server profile management and support for the `scottibyte-assist` portal-launch protocol.

The Windows installer registers the protocol so a server portal can configure and open the installed client. Provider credentials are maintained independently for each configured server.

## Ubuntu Linux

Ubuntu Linux 2.1.0 provides the same server profile management, server-specific provider authorization, immediate server switching, and portal-launch support as the Windows client.

The Debian package registers the `scottibyte-assist` URL scheme through the application desktop entry on both X11 and Wayland installations.

## Server

Assist Server **1.4.0** adds the portal-configured client launch feature and publishes metadata for both 2.1.0 clients.

Docker images:

```text
scottibyte/scottibyte-assist-server:1.4.0
scottibyte/scottibyte-assist-server:latest
```

## Client downloads

### Windows

`ScottiBYTE-Assist-Setup-2.1.0.exe`

SHA-256:

`c3797944f83b72cdc065d87818a7bf930c95a8945ee7b730240da7d7a4433a67`

### Ubuntu Linux

`ScottiBYTE-Assist_2.1.0_amd64.deb`

SHA-256:

`64aa0585be8a04f76b181aaacd750df2e9d50f9079183e95cd68df3f9cd6546d`
