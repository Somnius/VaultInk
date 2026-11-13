#pragma once

#include <Translate.h>

#include <string>

// One translation of a selection over HTTP(S). Wi-Fi must be up already. Where it goes:
//  * the user's LibreTranslate server when TRANSLATE_STORE has an address (its certificate is
//    checked only with the Check certificate switch on, like every server of the user's own);
//  * MyMemory otherwise, with no key, its certificate always checked (lib/TlsPolicy).
// Only the selection and the two language codes are sent. No more than
// translate::MAX_BODY_BYTES of the answer are read.
namespace translate_client {

enum class Result : uint8_t {
  Ok,           // `out.text` holds the translation
  NotFound,     // an answer with no translation in it
  NoQuery,      // nothing to send, or the selection is over translate::MAX_QUERY_BYTES
  NoPair,       // no language to translate from (MyMemory needs it) or into
  BadServer,    // the server address in the settings is not an http(s) address
  LowMemory,    // not enough free heap for a TLS session
  NoClock,      // the date could not be set, so the certificate cannot be checked
  NoCa,         // ca.pem is there but cannot be read
  Network,      // no answer (DNS, connect, the certificate check, a timeout)
  Quota,        // the day's free allowance is used up, or the server says slow down
  BadLanguage,  // the service does not know this pair of languages
  Refused,      // the server refused (API key, banned)
  ServerError,  // the service failed
  BadAnswer,    // an answer that is not a translation
};

// True when the user's own server is asked (and so named in the credit and the notice).
bool usesOwnServer();

Result fetch(const translate::Pair& pair, const std::string& text, translate::Result& out);

}  // namespace translate_client
