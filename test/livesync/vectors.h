#pragma once

// Generated from the real LiveSync library (octagonal-wheels 0.1.55, fixed randomness) by
// .work/livesync-src/vectors/gen.mjs; synthetic data only.
namespace vectors {
constexpr const char* PASSPHRASE = "test-pass";
constexpr const char* SALT_B64 = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=";
constexpr const char* MASTER_HEX = "0efbc1df27f0a308b0264967104ad6cb526bbce526a94b7054a19fa8a6bc5422";
constexpr const char* CHUNK_DATA =
    "%=wMHCw8TFxsfIycrLoKGio6SlpqeoqaqrrK2ur7CxsrO0tba3uLm6u7y9vr97gsS4iBJAQpVSEksBFfiNfdyOMheYOLj8j/"
    "MFwcU8IFJOMBlZ4ZEO5HIMnbPJHQ==";
constexpr const char* CHUNK_PLAIN = "Hello VaultInk Καλημέρα";
constexpr const char* META_FIELD =
    "/\\:%=JCUmJygpKissLS4vBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHyAhIiMsC2LYmdRTla3ANqwk+EnZmLUzBG+"
    "0BBcgx5doQ4OQKuOO8EnYy8m4yLeHk8GH2rgWvtAkvMVXZFkP819d1SZIL3zrimRjE2lrM4Ui17kX0743QClFkq/"
    "vnGQAAsKxAxbtYkYoBO5CUhK+PzNWFYQmzO0gvkcClEzmEbo8y4Vqxg==";
constexpr const char* META_JSON =
    "{\"path\":\"Notes/"
    "Hello.md\",\"mtime\":1760000000000,\"ctime\":1760000000000,\"size\":23,\"children\":[\"h:+exampleChunkId\"]}";
constexpr const char* URI_PASSPHRASE = "uri-pass";
constexpr const char* SETUP_URI =
    "obsidian://"
    "setuplivesync?settings=%25%24aGlqa2xtbm9wcXJzdHV2d3h5ent8fX5%"
    "2FgIGCg4SFhoeoqaqrrK2ur7CxsrOIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp%2BgoaKjpKWmp8HFvkUVhX55A7O%2FzoRnkyTxSJv5E75DESl6x%"
    "2Fio2IPHRb0SJgImPv%2BWkXe6aGbCPxzf0qiA0nZajpdNAEBNyISKV7N1XzNqqDMjp%2FrT8wCQjVBBKrdobY%2BEIQYBgdnCIGwlZ9t%"
    "2F5vgUsbgDDkRSoUUAi00gE4tSaoQvn3SHi4Wam0aSyNw0B%2B83tVOV%2FY5PXZY7cnL4Gla2LRFPoVHeO2gYhLf%"
    "2FPGHIJce3fOZGhGu9i5QZl6jJ57TdApjFAaNKNj4vRXh2SNpo05h%2Bl3u16W4QDUH5%2FbDEhWpAWeZaIHIhT7Rh6Q%"
    "2BwVPM13n96MWciFWp6wGl39XxCkhbpXMPmj282GRCRvIaBGFXk2qEVx6MSmPhy6vLFiOdBkAUnLxRQHjMzePOokfqzWpUR4EGNc9Kg0C0CPOQ15QZ"
    "N4erxypy6ETd9swl6HCr%"
    "2Bku5W7qNA9RROkOnWjdHtNH7pXV7Q3NF0I4sAIrDHcP6yaVdCUbluSmy2tfQtf6HaShIhui4ptWbwoQAQcAUCnaZOdG4XqxfwqO%"
    "2FP0y7rYJfCiVwKAqSf0bijZOzg6m3bIMWSDJkvsaHD5xgsJkmQY9y9q229x3d4qSiItEB029wZru4lmwDDbW1q9FLT9kZ20w7jnlqMns6raYz9tLN"
    "i333fuWRNSS%2B0gB1vYqNTyt4X6lrH6rXPachz2MM1ftR8jAo%3D";
constexpr const char* SETUP_BLOB =
    "%$aGlqa2xtbm9wcXJzdHV2d3h5ent8fX5/"
    "gIGCg4SFhoeoqaqrrK2ur7CxsrOIiYqLjI2Oj5CRkpOUlZaXmJmam5ydnp+goaKjpKWmp8HFvkUVhX55A7O/zoRnkyTxSJv5E75DESl6x/"
    "io2IPHRb0SJgImPv+WkXe6aGbCPxzf0qiA0nZajpdNAEBNyISKV7N1XzNqqDMjp/rT8wCQjVBBKrdobY+EIQYBgdnCIGwlZ9t/"
    "5vgUsbgDDkRSoUUAi00gE4tSaoQvn3SHi4Wam0aSyNw0B+83tVOV/Y5PXZY7cnL4Gla2LRFPoVHeO2gYhLf/"
    "PGHIJce3fOZGhGu9i5QZl6jJ57TdApjFAaNKNj4vRXh2SNpo05h+l3u16W4QDUH5/"
    "bDEhWpAWeZaIHIhT7Rh6Q+"
    "wVPM13n96MWciFWp6wGl39XxCkhbpXMPmj282GRCRvIaBGFXk2qEVx6MSmPhy6vLFiOdBkAUnLxRQHjMzePOokfqzWpUR4EGNc9Kg0C0CPOQ15QZN4"
    "erxypy6ETd9swl6HCr+"
    "ku5W7qNA9RROkOnWjdHtNH7pXV7Q3NF0I4sAIrDHcP6yaVdCUbluSmy2tfQtf6HaShIhui4ptWbwoQAQcAUCnaZOdG4XqxfwqO/"
    "P0y7rYJfCiVwKAqSf0bijZOzg6m3bIMWSDJkvsaHD5xgsJkmQY9y9q229x3d4qSiItEB029wZru4lmwDDbW1q9FLT9kZ20w7jnlqMns6raYz9tLNi3"
    "33fuWRNSS+0gB1vYqNTyt4X6lrH6rXPachz2MM1ftR8jAo=";
constexpr const char* SETTINGS_JSON =
    "{\"couchDB_URI\":\"https://"
    "couch.example.org\",\"couchDB_USER\":\"user\",\"couchDB_PASSWORD\":\"pw\",\"couchDB_DBNAME\":\"vault\","
    "\"encrypt\":true,\"passphrase\":\"test-pass\",\"usePathObfuscation\":true,\"E2EEAlgorithm\":\"v2\",\"hashAlg\":"
    "\"xxhash64\",\"chunkSplitterVersion\":\"v3-rabin-karp\",\"customChunkSize\":60,\"enableCompression\":false,"
    "\"useEden\":false,\"useDynamicIterationCount\":false,\"handleFilenameCaseSensitive\":false,"
    "\"configPassphraseStore\":\"\",\"encryptedCouchDBConnection\":\"\",\"encryptedPassphrase\":\"\","
    "\"encryptedIdDerivationKey\":\"\"}";
}  // namespace vectors
