#include "asynclib.h"

AsyncServerSocket::AsyncServerSocket() {}
AsyncServerSocket::~AsyncServerSocket() {}

bool AsyncServerSocket::makeNonBlocking(int fd) {
  return fcntl(fd, F_SETFL /* set file blocking status to */,
               fcntl(fd, F_GETFL, 0) | O_NONBLOCK /* non-block */) < 0;
}

bool AsyncServerSocket::listen(const char *port) {
  return this->listen(nullptr, port);
}
bool AsyncServerSocket::listen(const char *host, const char *port) {
  addrinfo addr =
               {
                   /*
                    If the AI_PASSIVE flag is specified in hints.ai_flags,
                    and node is NULL, then the returned socket addresses
                    will be suitable for bind(2)ing a socket that will
                    accept(2) connections. The returned socket address will
                    contain the "wildcard address" (INADDR_ANY for IPv4
                    addresses, IN6ADDR_ANY_INIT for IPv6 address).
                    */
                   .ai_flags = AI_PASSIVE,     /* for get a server socket */
                   .ai_family = AF_INET,       /* ipv4 */
                   .ai_socktype = SOCK_STREAM, /* tcp socket */
               },
           *result;
  if (::getaddrinfo(NULL, port, &addr, &result) < 0) {
    perror("getaddrinfo");
    return false;
  }

  if ((this->socketFd = ::socket(result->ai_family, result->ai_socktype,
                                 result->ai_protocol)) <
          0 /* if socket created error */
      || (host != nullptr &&
          strlen(host) > NI_MAXHOST /* or host is too large */)) {
    perror("socket");
    return false;
  }

  if (::bind(this->socketFd, result->ai_addr, result->ai_addrlen) < 0) {
    perror("bind");
    return false;
  }

  if (host != nullptr)
    memcpy(&this->hostname, &host, strlen(host));
  AsyncServerSocket::makeNonBlocking(this->socketFd);
  return ::listen(this->socketFd, SOMAXCONN) >= 0;
}

// TODO:
void AsyncServerSocket::on(
    AsyncServerSocket::Event event,
    std::function<void(AsyncServerSocket &socket)> callback) {
  if (event == AsyncServerSocket::Event::Connection) {
    this->connectionCallback = callback;
  }
}
int AsyncServerSocket::getSocketFd() { return this->socketFd; }