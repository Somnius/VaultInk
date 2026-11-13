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
  switch (server.protocol) {
    case RemoteServer::Protocol::Ftp:
      return makeFtpClient(server, error);
    case RemoteServer::Protocol::Sftp:
      return makeSftpClient(server, error);
    case RemoteServer::Protocol::WebDav:
    default:
      return makeWebDavClient(server, error);
  }
}

}  // namespace remotefs
