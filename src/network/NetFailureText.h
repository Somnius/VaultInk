#pragma once

#include <I18n.h>
#include <TlsPolicy.h>

// The line a screen shows for HttpDownloader::lastFailure(): one sentence per cause, so a missing date, a
// certificate that was not accepted and a missing network do not look the same. nullptr when there is nothing
// more exact to say than the screen's own "failed" text.
inline const char* netFailureText(const tlspolicy::Failure failure) {
  switch (failure) {
    case tlspolicy::Failure::NoNetwork:
      return tr(STR_CLOCK_SYNC_NO_WIFI);
    case tlspolicy::Failure::NoClock:
      return tr(STR_NET_NO_CLOCK);
    case tlspolicy::Failure::Unreachable:
      return tr(STR_FEED_UNREACHABLE);
    case tlspolicy::Failure::Certificate:
      return tr(STR_NET_CERT_REJECTED);
    case tlspolicy::Failure::None:
    case tlspolicy::Failure::Other:
    default:
      return nullptr;
  }
}
