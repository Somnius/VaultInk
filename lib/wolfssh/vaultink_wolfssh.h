/* vaultink_wolfssh.h
 *
 * The one header VaultInk code includes to use the vendored wolfSSH: the build options first, then
 * the public wolfSSH headers. Not an upstream file.
 *
 * Distributed under the same terms as wolfSSH: the GNU General Public License, version 3 or (at
 * your option) any later version.
 */
#ifndef VAULTINK_WOLFSSH_H
#define VAULTINK_WOLFSSH_H

#include "vaultink_wolfssh_config.h"

#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VaultInk additions, defined in vaultink/ws_wolfsftp.c: a folder read in batches instead of
 * wolfSSH_SFTP_LS()'s one unbounded list. Blocking sessions only. */
int VaultInk_SFTP_OpenDir(WOLFSSH* ssh, const char* dir, byte* handle, word32* handleSz);
WS_SFTPNAME* VaultInk_SFTP_ReadDir(WOLFSSH* ssh, byte* handle, word32 handleSz);

#ifdef __cplusplus
}
#endif

#endif /* VAULTINK_WOLFSSH_H */
