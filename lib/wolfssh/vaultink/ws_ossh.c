/* VaultInk wrapper: compiles the vendored wolfSSH src/ossh.c with the options in
 * vaultink_wolfssh_config.h, and only in a build that sets VAULTINK_SSH=1. See ../UPSTREAM.md. */
#if defined(VAULTINK_SSH) && VAULTINK_SSH
#include "vaultink_wolfssh_config.h"

#include "src/ossh.c"
#endif
