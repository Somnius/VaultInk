/* myFilesystem.h
 *
 * The WOLFSSH_USER_FILESYSTEM layer of VaultInk's wolfSSH build: no filesystem. wolfSSH's SFTP
 * source needs these names to compile; every call here fails, so the file-based wolfSSH functions
 * (wolfSSH_SFTP_Get/Put, wolfSSH_ReadKey_file, ...) return an error if anything ever calls them.
 * The firmware reads and writes the SD card itself and gives wolfSSH buffers only. Not an upstream
 * file (upstream ships a similar dummy in ide/STM32CUBE/myFilesystem.h).
 *
 * Distributed under the same terms as wolfSSH: the GNU General Public License, version 3 or (at
 * your option) any later version.
 */
#ifndef VAULTINK_MY_FILESYSTEM_H
#define VAULTINK_MY_FILESYSTEM_H

#ifdef WOLFSSH_USER_FILESYSTEM

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#define WFILE FILE
#define WFD int
#define WBADFILE NULL
#define WS_DELIM '/'
#define WSEEK_END SEEK_END

#define WOLFSSH_O_RDWR 0x0002
#define WOLFSSH_O_RDONLY 0x0000
#define WOLFSSH_O_WRONLY 0x0001
#define WOLFSSH_O_APPEND 0x0008
#define WOLFSSH_O_CREAT 0x0200
#define WOLFSSH_O_TRUNC 0x0400
#define WOLFSSH_O_EXCL 0x0800

typedef struct VaultInkNoStat {
  int unused;
} VaultInkNoStat;
#define WSTAT_T VaultInkNoStat

static inline int vaultink_nofs_fail(void) { return -1; }
static inline size_t vaultink_nofs_zero(void) { return 0; }
static inline char* vaultink_nofs_null(void) { return NULL; }

#define WFOPEN(fs, f, fn, m) ((void)(fs), (void)(fn), (void)(m), *(f) = WBADFILE, vaultink_nofs_fail())
#define WFCLOSE(fs, f) ((void)(fs), (void)(f), vaultink_nofs_fail())
#define WFREAD(fs, b, s, a, f) ((void)(fs), (void)(b), (void)(s), (void)(a), (void)(f), vaultink_nofs_zero())
#define WFWRITE(fs, b, s, a, f) ((void)(fs), (void)(b), (void)(s), (void)(a), (void)(f), vaultink_nofs_zero())
#define WFSEEK(fs, s, o, w) ((void)(fs), (void)(s), (void)(o), (void)(w), vaultink_nofs_fail())
#define WFTELL(fs, s) ((void)(fs), (void)(s), (long)vaultink_nofs_fail())
#define WREWIND(fs, s) ((void)(fs), (void)(s))
#define WFSEEK_SUCCESS(r) ((r) == 0)
#define WFFLUSH(s) ((void)(s))

#define WOPEN(fs, f, m, p) ((void)(fs), (void)(f), (void)(m), (void)(p), vaultink_nofs_fail())
#define WCLOSE(fs, fd) ((void)(fs), (void)(fd), vaultink_nofs_fail())
#define WPREAD(fs, fd, b, s, o) ((void)(fs), (void)(fd), (void)(b), (void)(s), (void)(o), vaultink_nofs_fail())
#define WPWRITE(fs, fd, b, s, o) ((void)(fs), (void)(fd), (void)(b), (void)(s), (void)(o), vaultink_nofs_fail())

#define WGETCWD(fs, r, rSz) ((void)(fs), (void)(r), (void)(rSz), vaultink_nofs_null())
#define WRMDIR(fs, d) ((void)(fs), (void)(d), vaultink_nofs_fail())
#define WMKDIR(fs, d, m) ((void)(fs), (void)(d), (void)(m), vaultink_nofs_fail())
#define WREMOVE(fs, d) ((void)(fs), (void)(d), vaultink_nofs_fail())
#define WRENAME(fs, o, n) ((void)(fs), (void)(o), (void)(n), vaultink_nofs_fail())
#define WSTAT(fs, p, b) ((void)(fs), (void)(p), (void)(b), vaultink_nofs_fail())
#define WLSTAT(fs, p, b) ((void)(fs), (void)(p), (void)(b), vaultink_nofs_fail())
#define WCHMOD(fs, f, m) ((void)(fs), (void)(f), (void)(m), vaultink_nofs_fail())
#define WSETTIME(fs, f, a, m) ((void)(fs), (void)(f), (void)(a), (void)(m), vaultink_nofs_fail())

/* wolfsftp.c expects the user filesystem to supply this one as a function. */
static inline int SFTP_GetAttributes(void* fs, const char* fileName, void* atr, unsigned char noFollow, void* heap) {
  (void)fs;
  (void)fileName;
  (void)atr;
  (void)noFollow;
  (void)heap;
  return -1;
}

#endif /* WOLFSSH_USER_FILESYSTEM */

#endif /* VAULTINK_MY_FILESYSTEM_H */
