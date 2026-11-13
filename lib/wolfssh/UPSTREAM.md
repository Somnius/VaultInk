# wolfSSH, vendored

- Upstream: https://github.com/wolfSSL/wolfssh
- Version: 1.6.0, tag `v1.6.0-stable`, commit `8221e4ee48c93da6cea3325a91de2ecc00ae5f56`
  ("Prepare for release 1.6.0", 2026-10-06), fetched 2026-11-13.
- Licence: GPL-3.0-or-later. `LICENSING` is upstream's file ("either licensed for use under the
  GPLv3 or a standard commercial license"); the source headers say "either version 3 of the
  License, or (at your option) any later version". `COPYING` is the GPL version 3 text: upstream
  ships none, this copy is the SPDX text `GPL-3.0-or-later.txt`.

## What is here

| Path | From upstream | Changed |
| --- | --- | --- |
| `src/internal.c io.c log.c misc.c ossh.c port.c ssh.c wolfsftp.c` | `src/` | no, byte for byte |
| `wolfssh/*.h` | `wolfssh/` (without `test.h`, `certs_test.h`, `version.h.in`) | no, byte for byte |
| `LICENSING` | `LICENSING` | no |
| `vaultink_wolfssh_config.h`, `vaultink_wolfssh.h`, `myFilesystem.h`, `vaultink/ws_*.c`, `library.json` | not upstream: VaultInk's build glue, under the same licence | |

Not taken: the server and example programs (`apps/`, `examples/`), `agent.c`, `certman.c`,
`keygen.c`, `wolfscp.c`, `wolfterm.c`, the IDE projects, the tests and the test keys.

## How it is built

- PlatformIO compiles only `vaultink/ws_*.c` (`library.json`: `srcDir`). Each wrapper includes
  `vaultink_wolfssh_config.h` and then one upstream source, and is empty unless the build sets
  `VAULTINK_SSH=1` (`[vaultink_ssh]` in `platformio.ini`), so the other boards compile nothing.
- Options: a client only (`NO_WOLFSSH_SERVER`), SFTP (`WOLFSSH_SFTP`), the pty request
  (`WOLFSSH_TERM`, `NO_TERMIOS`), the firmware's own socket callbacks (`WOLFSSH_USER_IO`), no
  finite-field DH (`WOLFSSH_NO_DH`), a 32 KB window, 16 KB packets, 8 KB SFTP requests.
- wolfSSL (Arduino-wolfSSL 5.7.2) is built with `WOLFSSL_WOLFSSH`, `HAVE_ED25519`,
  `WOLFSSL_ED25519_STREAMING_VERIFY` and `WOLFSSL_SHA512` in the same builds.
- Arduino-wolfSSL sets `NO_FILESYSTEM`; `wolfsftp.c` does not compile without a filesystem layer.
  The config header takes the define back for wolfSSH's own files and selects
  `WOLFSSH_USER_FILESYSTEM`; `myFilesystem.h` is a layer where every call fails. The firmware uses
  the buffer functions only (`wolfSSH_SFTP_Open`, `SendReadPacket`, `SendWritePacket`, ...).
- VaultInk code includes `<vaultink_wolfssh.h>`, never the wolfSSH headers directly, so it sees the
  same options as the library.

## Updating

Replace the files in `src/` and `wolfssh/` from the new tag, update this file, `library.json` and
`docs/third-party.json`, run `scripts/gen_licenses.py`, then the host checks in `docs/ssh.md`.
