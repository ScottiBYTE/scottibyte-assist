# ScottiBYTE Assist 2.0.3

ScottiBYTE Assist 2.0.3 is a corrective Windows and Ubuntu Linux client release.

## Highlights

- Fixed Windows remote I-beam cursor visibility on both light and dark desktop backgrounds.
- Added the Linux `pulseaudio-utils` dependency required for reliable remote desktop audio source discovery.
- Retains the Customer Terminal, remote desktop, voice, chat, clipboard, file transfer, and application-sharing improvements introduced in the 2.0 series.

## Windows

Windows 2.0.3 improves remote cursor rendering for Windows customers.

Windows monochrome cursors can use XOR/invert behavior rather than ordinary color and alpha pixels. The remote I-beam cursor could therefore become effectively invisible over light backgrounds. ScottiBYTE Assist now converts these cursor pixels to a high-contrast representation that remains visible on both light and dark content.

## Ubuntu Linux

Ubuntu Linux 2.0.3 adds `pulseaudio-utils` as a package dependency.

ScottiBYTE Assist uses `pactl` to identify the default output sink and its PipeWire/PulseAudio monitor source for remote desktop audio. Installing the dependency automatically ensures this functionality is available on fresh installations, including Wayland systems using PipeWire.

## Server

The ScottiBYTE Assist server release remains **1.3.2**.

Docker images:

```text
scottibyte/scottibyte-assist-server:1.3.2
scottibyte/scottibyte-assist-server:latest
```

## Client downloads

### Windows

`ScottiBYTE-Assist-Setup-2.0.3.exe`

SHA-256:

`17f49b3249bd336b62b791023329794cfdcb74b92cd8c073eafb802789ed0341`

### Ubuntu Linux

`ScottiBYTE-Assist_2.0.3_amd64.deb`

SHA-256:

`6946eef9390f333649ccdac355e748b7444cae5e442c68b28ccd447d44e7ddb9`
