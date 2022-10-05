#pragma once
#include <errno.h>
#include <fcntl.h>
#include <functional>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

/* generator macro */
#define async_context_start                                                    \
  static int state = 0;                                                        \
  switch (state) {                                                             \
  case 0:
#define async_context_end }
#define await(x)                                                               \
  {                                                                            \
    state = __LINE__;                                                          \
    return x;                                                                  \
  case __LINE__:;                                                              \
  }

/* async lib */
class AsyncServerSocket {
public:
  enum State {
    Closed,
  };
  enum Event {
    Connection,
  };
  AsyncServerSocket();
  ~AsyncServerSocket();
  static bool makeNonBlocking(int fd);
  bool listen(const char *port);
  bool listen(const char *host, const char *port);
  void on(AsyncServerSocket::Event event,
          std::function<void(AsyncServerSocket &socket)> callback);
  int getSocketFd();

private:
  int socketFd = -1;
  char hostname[NI_MAXHOST] = {0};
  AsyncServerSocket::State state = Closed;
  std::function<void(AsyncServerSocket &socket)> connectionCallback = nullptr;
};

class EventLoop {
  using EventHandler =
      std::function<void(char *buffer, uint len,
                         std::function<void(const char *buffer, uint len)>)>;
  using FileDescriptor = int;

public:
  enum Event {
    WRITE,
    READ,
    ERROR,
  };
  EventLoop(uint cnt = 1024);
  ~EventLoop();
  bool init();
  bool regist(AsyncServerSocket &socket, EventLoop::Event eventType,
              EventHandler callback);
  bool registClient(FileDescriptor &socket);
  bool unregist(AsyncServerSocket &socket, EventLoop::Event eventType);
  void run();

private:
  static const int WAIT_FOREVER = -1;
  int epollFd = -1;
  uint maxEventsCount;
  epoll_event *events;
  // client socket -> server socket
  std::unordered_map<FileDescriptor, FileDescriptor> fdClientToServerMap;
  // server socket -> handlers map
  std::unordered_map<
      FileDescriptor,
      std::unordered_map<EventLoop::Event, std::vector<EventHandler>>>
      fdEventMap;

  template <typename K, typename V>
  bool has(std::unordered_map<K, V> map, K key);
  void readAndCall(FileDescriptor fd);
  std::function<void(const char *buffer, uint len)>
  closureWriteTo(FileDescriptor fd);
};
