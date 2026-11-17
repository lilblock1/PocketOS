# PocketOS R1 system update 3 — Classic UI

The owner tested the functional Classic UI on the R1, approved it, and requested
publication through Wi-Fi updates. The pre-release channel now points to
[pocketos-test-3](https://github.com/lilblock1/PocketOS/releases/tag/pocketos-test-3).
Stable is unchanged. A channel check or download does not authorize installation;
the device retains its separate Install confirmation.

The full system update enables the approved UI at startup and adds the optional
Classic reader theme. It retains the verified update-2 system, replacing only
the shell, startup script and release/UI metadata. The primary kernel payload
is byte-identical to the proven kernel; bootloader, recovery and userdata have
no payload. Existing music, EPUB, physical volume and Wi-Fi updater behavior is
retained. Detailed settings remain full-screen touch controls. Light/dark mode,
further optimization and Bluetooth reconnect/disconnect fixes are deferred.

- Source: `512ea866114ed25117f0f952f68343a965a21310` on `phase6-ui-review`.
- Application builds: [UI](https://github.com/lilblock1/PocketOS/actions/runs/44197905820)
  and [startup integration](https://github.com/lilblock1/PocketOS/actions/runs/44198741200) passed.
- Sequence 3; build `fc948d28ad584f2c`; UPT 16,279,552 bytes.
- UPT SHA-256: `994cd5ed069acb1077ee5785ec46b59233a95165d33fa7718e0f291568ec1485`.
- The manifest signature verifies against the retained update trust. All four
  public update assets were downloaded and compared byte-for-byte to local files.
- Exact kernel/rootfs reconstruction, updater staging, target tests and startup
  checks passed. The file-only writer fixture confirms primary kernel/rootfs
  targets, primary boot selection restoration and both completion markers.

UI preview RAM was around 2.7 MiB, with a 2.8 MiB peak. A matched-state reduction
versus update 2 has not been proven. The owner accepted further optimization as
later work. Sequence-3 installation/cold boot is not yet physically verified;
no firmware install was performed while publishing this channel update.

Use Settings → Updates → Pre-release on the R1. Signed SD staging assets are
also attached to the release for the existing PocketOS-inbox flow.
No personal files, credentials, device logs or private signing material are
included in this source update or release.
