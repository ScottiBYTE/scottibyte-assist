# ScottiBYTE Assist Linux 2.2.3

## Changes

- Use absolute Wayland pointer coordinates mapped to the selected portal stream and EIS region; remove the relative-motion fallback that caused oversensitive control.
- Avoid showing unparented startup labels as temporary windows, eliminating the observed launch wait cursor.
- Improve portal request cancellation, timeouts, stale-response handling, and cleanup following session closure or portal service restarts.
- Honor valid PipeWire window crop metadata, preserving actual window content.
- Size the provider-screen viewer to the shared frame, with a contrasting surround and visible border. Preserve aspect ratio and manual viewer resizing.
- Retain the single amd64 Debian installer built on Ubuntu 24.04 for Ubuntu 24.04 and 26.04.

## Validation

The combined test build was tested on live Wayland desktops: pointer control was smooth, the startup wait cursor was resolved, and shared-window cropping and viewer sizing worked. Installation and dependency checks for the production package were run in Ubuntu 24.04 and 26.04 Incus containers.

The intermittent portal-unavailability issue has lifecycle improvements in this release; a long-term resolution has not yet been established. These container checks do not substitute for graphical session testing.

Windows remains at 2.2.0. Assist Server 1.4.5 bundles this Linux installer and updated portal release information; its signaling protocol is unchanged.

## Installation

Close Assist, then run:

```bash
sudo apt install ./ScottiBYTE-Assist_2.2.3_amd64.deb
```

## SHA-256

See the accompanying `ScottiBYTE-Assist_2.2.3_amd64.deb.sha256` file.
