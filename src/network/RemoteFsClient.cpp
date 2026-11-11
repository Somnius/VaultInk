#include "RemoteFsClient.h"

#include "RemoteServerStore.h"

namespace remotefs {

std::unique_ptr<Client> makeClient(const RemoteServer& server, const bool plainConfirmed, Error& error) {
  // The one gate for a connection that is not encrypted: without the user's yes on the screen for
  // this connection, nothing is sent.
  if (server.isPlain() && !plainConfirmed) {
    error = Error::PlainRefused;
    return nullptr;
  }
  return server.protocol == RemoteServer::Protocol::Ftp ? makeFtpClient(server, error)
                                                        : makeWebDavClient(server, error);
}

}  // namespace remotefs
