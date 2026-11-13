/* vaultink_wolfssh_config.h
 *
 * VaultInk's build options for the vendored wolfSSH (see ../UPSTREAM.md). Not an upstream file.
 * Included first by every wrapper in this folder and by vaultink_wolfssh.h, so the library and
 * its callers always see the same options.
 *
 * This file is part of the VaultInk build of wolfSSH and is distributed under the same terms as
 * wolfSSH: the GNU General Public License, version 3 or (at your option) any later version.
 */
#ifndef VAULTINK_WOLFSSH_CONFIG_H
#define VAULTINK_WOLFSSH_CONFIG_H

/* The wolfSSL options first: the build passes -DWOLFSSL_USER_SETTINGS and the -D list
 * ([vaultink_ssh] in platformio.ini adds WOLFSSL_WOLFSSH, HAVE_ED25519 and WOLFSSL_SHA512). */
#include <wolfssl/wolfcrypt/settings.h>
/* wolfssh/settings.h includes wolfSSL's user_settings.h once more (that file has no include
 * guard), so it is pulled in here, before the NO_FILESYSTEM change below, and not again later. */
#include <wolfssh/settings.h>

/* Arduino-wolfSSL's user_settings.h sets NO_FILESYSTEM for wolfSSL. wolfSSH's SFTP source only
 * compiles with a filesystem layer, so inside wolfSSH (and nowhere else) the define is taken back
 * and the layer is myFilesystem.h in this folder, where every call fails. The firmware never calls
 * the file-based wolfSSH functions (wolfSSH_SFTP_Get/Put, the *_file key loaders); it moves the
 * bytes itself through wolfSSH_SFTP_SendReadPacket / SendWritePacket. */
#undef NO_FILESYSTEM
#define WOLFSSH_USER_FILESYSTEM

/* A client only. */
#define NO_WOLFSSH_SERVER
#define WOLFSSH_SFTP
/* The pty request for the terminal screen; there is no termios on the device. */
#define WOLFSSH_TERM
#define NO_TERMIOS
/* The firmware gives wolfSSH its own send/receive callbacks (lib/SshClient). */
#define WOLFSSH_USER_IO

/* Memory. Upstream defaults: a 128 KB receive window, 32 KB channel packets, 32 KB of file data
 * per SFTP request. */
#define DEFAULT_WINDOW_SZ 32768
#define DEFAULT_MAX_PACKET_SZ 16384
#define WOLFSSH_MAX_SFTP_RW 8192

/* No finite-field Diffie-Hellman: curve25519-sha256 and ecdh-sha2-nistp256 are offered. */
#define WOLFSSH_NO_DH

#endif /* VAULTINK_WOLFSSH_CONFIG_H */
