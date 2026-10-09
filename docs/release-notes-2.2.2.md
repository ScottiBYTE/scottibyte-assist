# ScottiBYTE Assist Linux 2.2.2

## Overview

Linux 2.2.2 introduces a portable Debian installer compatible with
Ubuntu 24.04 LTS and Ubuntu 26.04 LTS.

This maintenance release addresses library compatibility problems
encountered when running the previous Linux installer on Ubuntu 24.04.

## Changes

- Build the Linux client against Ubuntu 24.04's Qt 6.4 libraries.
- Support installation on both Ubuntu 24.04 and Ubuntu 26.04.
- Bundle WebRTC audio-processing libraries and required Abseil dependencies.
- Improve Debian package dependency compatibility across Ubuntu LTS releases.
- Add packaging-linux/build-portable.sh for portable installer builds.
- Preserve existing remote assistance, desktop sharing, and control features.

## Compatibility

| Component | Version |
|---|---|
| Ubuntu Linux client | 2.2.2 |
| Windows client | 2.2.0 |
| Assist server | 1.4.4 |

## Validation

The following functionality was verified during testing:

- Installation and application launch on Ubuntu 24.04 and Ubuntu 26.04.
- Screen sharing and remote desktop control with Ubuntu 24.04.
- Remote assistance between Ubuntu 26.04 and Windows 11.
- Keyboard and mouse control during remote sessions.

Clipboard synchronization and voice communication were not separately
validated as part of this maintenance release.

## Installation

Download ScottiBYTE-Assist_2.2.2_amd64.deb.

Install using:

    sudo apt install ./ScottiBYTE-Assist_2.2.2_amd64.deb

Close any running ScottiBYTE Assist processes before upgrading.
If unusual keyboard behavior occurs immediately after upgrading,
log out of Ubuntu and log back in before starting Assist again.

## Building from source

The portable build script uses an Ubuntu 24.04 Incus container named
ScottiBYTE-Build, with the project mounted at /mnt/scottibyte-assist.

The container must have the required compiler, Qt development packages,
WebRTC audio-processing library, Abseil libraries, and packaging tools
installed.

Run:

    ./packaging-linux/build-portable.sh

The resulting Debian package is placed in packaging-linux/output/.

## SHA-256

ScottiBYTE-Assist_2.2.2_amd64.deb

    71a2eb05a0b8ec2ee3c7717b5d58f002741323a57e93051e0bcd9cb14f4c926c
