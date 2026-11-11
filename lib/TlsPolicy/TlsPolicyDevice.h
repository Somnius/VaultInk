#pragma once

#include <SecureHttpClient.h>
#ifndef SIMULATOR
#include <SecureClient.h>
#endif

#include <string>

#include "TlsPolicy.h"

// The device side of TlsPolicy.h: the card, the clock and the built-in roots.
namespace tlspolicy {

// The user's own trust anchor for self-hosted servers that have Check certificate on. (LiveSync
// keeps its own file, /.vaultink/livesync/ca.pem.)
inline constexpr char CA_FILE[] = "/.crosspoint/ca.pem";

// Sets `http` up for a request that starts at `url`; call it before the request, for every hop of
// a redirect chain with the URL the chain started at, so a redirect cannot lower the check.
// `userVerify`: the user turned Check certificate on for this server (ignored for GitHub and
// Wikipedia, which are always checked).
// Returns false when the request must not be sent; `http` is then left with no trust anchors, so
// sending anyway fails at the handshake instead of going out unchecked.
bool apply(freeink::SecureHttpClient& http, const std::string& url, bool userVerify, Error* error = nullptr);
// The same for a bare TLS connection (the WebDAV upload writes its own request, because the HTTP
// client can only send a body that is already in memory).
// (Not in the simulator, whose network shims have no bare TLS client.)
#ifndef SIMULATOR
bool apply(freeink::SecureClient& tls, const std::string& url, bool userVerify, Error* error = nullptr);
#endif

// True when the shared ca.pem is on the card.
bool hasOwnCa();

}  // namespace tlspolicy
