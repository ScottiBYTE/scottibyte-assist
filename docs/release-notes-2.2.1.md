# ScottiBYTE Assist Linux 2.2.1

- Fix bidirectional Wayland text clipboard synchronization when Assist gains focus.
- Check advertised portal cursor capabilities before requesting screen sharing.
- Improve provider connection fallback timing.
- Package the required WebRTC audio and Abseil libraries.

Windows remains at 2.2.0. Server/portal image 1.4.3 includes the updated Linux installer.

A GNOME desktop portal crash was observed during testing. Restarting the portal restored sharing; the underlying crash is not claimed fixed.
