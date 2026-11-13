/* VaultInk wrapper: compiles the vendored wolfSSH src/wolfsftp.c with the options in
 * vaultink_wolfssh_config.h, and only in a build that sets VAULTINK_SSH=1. See ../UPSTREAM.md. */
#if defined(VAULTINK_SSH) && VAULTINK_SSH
#include "../vaultink_wolfssh_config.h"

#include "../src/wolfsftp.c"

/* Reading a folder in pieces. Upstream's public wolfSSH_SFTP_LS() reads the whole folder into
 * one linked list before it returns, which has no bound on a small heap. These two give the
 * firmware the steps LS is made of (open the folder, then one batch of names per call), so it can
 * stop at its own cap. They sit in this wrapper because wolfSSH_SFTP_GetHandle() is static in
 * wolfsftp.c; no upstream file is changed. For a session whose receive callback blocks: they are
 * not written to be called again after WS_WANT_READ. */
int VaultInk_SFTP_OpenDir(WOLFSSH* ssh, const char* dir, byte* handle, word32* handleSz);
WS_SFTPNAME* VaultInk_SFTP_ReadDir(WOLFSSH* ssh, byte* handle, word32 handleSz);

int VaultInk_SFTP_OpenDir(WOLFSSH* ssh, const char* dir, byte* handle, word32* handleSz)
{
    int ret;

    if (ssh == NULL || dir == NULL || handle == NULL || handleSz == NULL)
        return WS_BAD_ARGUMENT;
    ret = wolfSSH_SFTP_OpenDir(ssh, (byte*)dir, (word32)WSTRLEN(dir));
    if (ret == WS_SUCCESS)
        ret = wolfSSH_SFTP_GetHandle(ssh, handle, handleSz);
    return ret;
}

/* One batch of names, to be freed with wolfSSH_SFTPNAME_list_free(). NULL at the end of the
 * folder and on an error. */
WS_SFTPNAME* VaultInk_SFTP_ReadDir(WOLFSSH* ssh, byte* handle, word32 handleSz)
{
    return wolfSSH_SFTP_ReadDir(ssh, handle, handleSz);
}
#endif
