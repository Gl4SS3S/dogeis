#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/_endian.h>
#include <sys/_types/_ssize_t.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#define BUFFER_SIZE 3

const size_t k_max_msg = 4096;

enum { STATE_REQ = 0, STATE_RES = 1, STATE_END = 2 };

struct Conn {
  int fd = -1;
  uint32_t state = 0;

  // Buffer for reading
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];

  // Buffer for writing
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    die("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);

  if (errno) {
    die("fcntl error");
  }
}

static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      return -1;
    }

    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }

  return 0;
}

static int32_t write_all(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv <= 0) {
      return -1;
    }

    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }

  return 0;
}

static int32_t one_request(int connfd) {
  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  int32_t err = read_full(connfd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      msg("EOF");
    } else {
      msg("read () error");
    }

    return err;
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    msg("Too long");
    return -1;
  }

  // request body
  err = read_full(connfd, &rbuf[4], len);
  if (err) {
    msg("read () error");
    return err;
  }

  // do something
  rbuf[4 + len] = '\0';
  printf("client says: %s\n", &rbuf[4]);

  // reply using the same protocol
  const char reply[] = "world";
  char wbuf[4 + sizeof(reply)];
  len = (uint32_t)strlen(reply);
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], reply, len);
  return write_all(connfd, wbuf, 4 + len);
}

static void do_something(int connfd) {
  char rbuf[64] = {};
  ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
  if (n < 0) {
    msg("read() error");
    return;
  }

  printf("client says: %s\n", rbuf);

  char wbuf[] = "world";
  write(connfd, wbuf, strlen(wbuf));
}

static bool try_fill_buffer(Conn *conn) {
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;

  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);

  if (rv < 0 && errno == EAGAIN) {
    // got EAGAIN, stop.
    return false;
  }

  if (rv < 0) {
    msg("read() error");
    conn->state = STATE_END;
    return false;
  }

  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      msg("unexpected EOF");
    } else {
      msg("EOF");
    }

    conn->state = STATE_END;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
}

static void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {
  }
}

static void connection_io(Conn *conn) {
  if (conn->state == STATE_END) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);
  }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }

  fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  // accept
  struct sockaddr_in client_addr = {};

  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    msg("accept() error");
    return -1;
  }

  fd_set_nb(connfd);
  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
    close(connfd);
    return -1;
  }

  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;
  conn_put(fd2conn, conn);
  return 0;
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);     // Port selection
  addr.sin_addr.s_addr = ntohl(0); // Wildcard address 0.0.0.0
  int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));

  if (rv) {
    die("bind()");
  }

  rv = listen(fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  std::vector<Conn *> fd2conn;

  fd_set_nb(fd);

  std::vector<struct pollfd> poll_args;

  while (true) {
    // Prepare Arguments of the poll
    poll_args.clear();

    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);

    for (Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }

      struct pollfd pfd = {};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : 0;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }

    // Poll for active fds
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (rv < 0) {
      die("poll");
    }

    for (size_t i = 1; i < poll_args.size(); i++) {
      if (poll_args[i].revents) {
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END) {
          // Client Closed - Kill connection
          fd2conn[conn->fd] = NULL;
          (void)close(conn->fd);
          free(conn);
        }
      }
    }

    if (poll_args[0].revents) {
      (void)accept_new_conn(fd2conn, fd);
    }
  }

  return 0;
}
