# ScottiBYTE Assist Server 1.4.5

- Bundle the Linux 2.2.3 installer and its SHA-256 checksum in the public download portal.
- Update portal release metadata and current-release documentation.
- Retain the Windows 2.2.0 installer.
- Publish Docker tags `scottibyte/scottibyte-assist-server:1.4.5` and `latest`.

Server signaling, authorization, and protocol behavior are unchanged. The client fixes are documented in [Linux 2.2.3 release notes](release-notes-2.2.3.md).

The image is checked without portal bind mounts to verify its bundled metadata and both client downloads. Existing deployments with portal mounts must update their mounted files as well.
