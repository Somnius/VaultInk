/* wolfCrypt asn.c looks up certificate authorities through wolfSSL (src/ssl.c), which this host
 * build leaves out: SSH has no certificates. The firmware links the real ones. */
void* GetCA(void* manager, unsigned char* hash);
void* GetCAByName(void* manager, unsigned char* hash);
void* GetCA(void* manager, unsigned char* hash) {
  (void)manager;
  (void)hash;
  return 0;
}
void* GetCAByName(void* manager, unsigned char* hash) {
  (void)manager;
  (void)hash;
  return 0;
}
