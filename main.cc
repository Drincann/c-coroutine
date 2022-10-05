#include "src/asynclib.h"

int main(int argc, char *argv[]) {
  AsyncServerSocket asyncSocket = AsyncServerSocket();
  if (!asyncSocket.listen("8081"))
    exit(1);

  EventLoop loop;
  loop.init();
  loop.regist(asyncSocket, EventLoop::Event::READ,
              [&](auto buffer, auto len, auto _) { printf(buffer); });
  loop.regist(asyncSocket, EventLoop::Event::WRITE,
              [&](auto buffer, auto len, auto write) {
                const char *buf =
                    "HTTP/1.1 200 OK\n\n<h1 style=\"text-align: center; "
                    "padding-top: 100px;\">Hello Coroutines!</h1>";
                write(buf, strlen(buf));
                delete[] buf;
              });
  loop.run();
  return EXIT_SUCCESS;
}
