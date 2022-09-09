#include <./asynclib>

class EventLoop {
  using EventHandler = std::function<void(
      char *buffer, uint len, std::function<void(char *buffer, uint len)>)>;
  using FileDescriptor = int;

public:
  enum Event {
    WRITE,
    READ,
    ERROR,
  };
  EventLoop(uint cnt = 1024) : maxEventsCount(cnt){};
  ~EventLoop() {
    if (this->epollFd > 0)
      close(this->epollFd);
    if (this->events != nullptr)
      delete[] this->events;
  }
  bool init() {
    this->epollFd = epoll_create1(0);
    this->events = new epoll_event[this->maxEventsCount];
    if (this->epollFd < 0 || this->events == nullptr) {
      perror("epoll_create1");
      exit(EXIT_FAILURE);
    }
  }

  bool regist(AsyncServerSocket &socket, EventLoop::Event eventType,
              EventHandler callback) {
    // epoll does not have an api to query for existence of a file descriptor
    // we have to use a map to store the file descriptor and its event type
    if (has(fdEventMap, socket.getSocketFd())) {
      if (has(fdEventMap[socket.getSocketFd()], eventType)) {
        fdEventMap[socket.getSocketFd()][eventType].emplace_back(callback);
      } else {
        fdEventMap[socket.getSocketFd()][eventType] = {callback};
      }
    } else {
      // we assert that the existence of the file descriptor in map is
      // consistent with its existence in epoll
      epoll_event event = {
          .events = EPOLLIN | EPOLLOUT | EPOLLET,
          .data = {.fd = socket.getSocketFd()},
      };
      if (epoll_ctl(this->epollFd, EPOLL_CTL_ADD, socket.getSocketFd(),
                    &event) < 0) {
        return false;
      }
      fdEventMap[socket.getSocketFd()] = {{eventType, {callback}}};
    }
    return true;
  }

  bool unregist(AsyncServerSocket &socket, EventLoop::Event eventType) {
    if (has(fdEventMap, socket.getSocketFd())) {
      if (has(fdEventMap[socket.getSocketFd()], eventType)) {
        fdEventMap[socket.getSocketFd()].erase(eventType);
      }
      if (fdEventMap[socket.getSocketFd()].empty()) {
        fdEventMap.erase(socket.getSocketFd());
        epoll_event event = {
            .events = EPOLLIN | EPOLLOUT | EPOLLET,
            .data = {.fd = socket.getSocketFd()},
        };
        // according to the above assertion, it should be removed from epoll
        // here
        return epoll_ctl(this->epollFd, EPOLL_CTL_DEL, socket.getSocketFd(),
                         &event) >= 0;
      }
    }
  }

  void run() {
    while (true) {
      /* I/O phase */
      int howManyTriggered = epoll_wait(
          this->epollFd, this->events, this->maxEventsCount,
          EventLoop::WAIT_FOREVER /* blocking forever if no event comming */
      );

      for (uint16_t i = 0; i < howManyTriggered; ++i) {
        FileDescriptor fdTriggered = this->events[i].data.fd;

        /* try accept connection on the file descriptor triggered */
        while (1) {
          sockaddr clientAddr;
          socklen_t clientAddrLen = sizeof(clientAddr);
          FileDescriptor clientSocketFd = -1;
          char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

          clientSocketFd = accept(fdTriggered, &clientAddr, &clientAddrLen);
          if (clientSocketFd == -1) {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
              /* We have processed all incoming
                 connections. */
              break;
            } else {
              perror("accept");
              break;
            }
          }

          /* Make the incoming socket non-blocking and add it to the epoll */
          if (AsyncServerSocket::makeNonBlocking(clientSocketFd) == -1) {
            close(clientSocketFd);
            continue;
          }

          // if(this->regist())
        }

        int eventsTriggered = this->events[i].events;
        if (eventsTriggered & EPOLLERR || eventsTriggered & EPOLLHUP) {
          if (has(fdEventMap, fdTriggered) &&
              has(fdEventMap[fdTriggered], Event::ERROR)) {
            for (auto callback : fdEventMap[fdTriggered][Event::ERROR]) {
              callback(nullptr, 0, nullptr);
            }
          }
        }
        if (eventsTriggered & EPOLLIN) {
          if (has(fdEventMap, fdTriggered) &&
              has(fdEventMap[fdTriggered], Event::READ)) {
            this->readAndCall(fdTriggered);
          }
        }
        if (eventsTriggered & EPOLLOUT) {
          if (has(fdEventMap, fdTriggered) &&
              has(fdEventMap[fdTriggered], Event::WRITE)) {
            for (auto callback : fdEventMap[fdTriggered][Event::WRITE]) {
              callback(nullptr, 0, this->closureWriteTo(fdTriggered));
            }
          }
        }
      }
    }
  }

private:
  int epollFd = -1;
  uint maxEventsCount;
  epoll_event *events;
  // TODO: Refactoring to server socket to client socket mapping
  std::unordered_map<
      FileDescriptor,
      std::unordered_map<EventLoop::Event, std::vector<EventHandler>>>
      fdEventMap;

  static const int WAIT_FOREVER = -1;

  template <typename K, typename V>
  bool has(std::unordered_map<K, V> map, K key) {
    return map.find(key) != map.end();
  }

  void readAndCall(FileDescriptor fd) {
    uint32_t readed = 0;
    uint32_t currentLength = 1024;
    char *buf = new char[currentLength];
    while (true) {
      int count = SocketLib::read(fd, buf + readed, currentLength - readed);
      if (count == -1) {
        if (errno == EAGAIN) {
          /* we have read all data */
          break;
        }
      } else if (count == 0) {
        /* remote has closed the connection. */
        break;
      }
      readed += count;
      if (readed == currentLength) {
        currentLength *= 2;
        char *newBuf = new char[currentLength];
        memcpy(newBuf, buf, readed);
        delete[] buf;
        buf = newBuf;
      }
    }
    if (readed > 0) {
      for (auto callback : fdEventMap[fd][Event::READ]) {
        callback(buf, readed, nullptr);
      }
    }
    delete[] buf;
  }

  std::function<void(char *buffer, uint len)>
  closureWriteTo(FileDescriptor fd) {
    return [fd](char *buffer, uint len) {
      uint written = 0;
      while (written < len) {
        int count = SocketLib::write(fd, buffer + written, len - written);
        if (count == -1) {
          if (errno == EAGAIN) {
            /* we have written all data */
            break;
          }
        } else if (count == 0) {
          /* remote has closed the connection. */
          break;
        }
        written += count;
      }
    };
  }
};
