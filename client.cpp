#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <ios>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/_endian.h>
#include <sys/_types/_ssize_t.h>
#include <sys/socket.h>
#include <unistd.h>

const size_t k_max_msg = 4096;

static void die(const char *msg) {
  int err = errno;
  fprintf(stderr, "[%d] %s\n", err, msg);
  abort();
}

static void msg(const char *msg) { fprintf(stderr, "%s\n", msg); }

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

static int32_t query(int fd, const char *text) {
  uint32_t len = (uint32_t)strlen(text);
  if (len > k_max_msg) {
    return -1;
  }

  char wbuf[4 + k_max_msg];
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], text, len);
  if (int32_t err = write_all(fd, wbuf, 4 + len)) {
    return err;
  }

  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  int32_t err = read_full(fd, rbuf, 4);
  if (err) {
    if (err) {
      if (errno == 0) {
        msg("EOF");
      } else {
        msg("read () error");
      }

      return err;
    }
  }

  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    msg("too long");
    return -1;
  }

  err = read_full(fd, &rbuf[4], len);
  if (err) {
    msg("read() error");
    return err;
  }

  rbuf[4 + len] = '\0';
  printf("server says %s\n", &rbuf[4]);
  return 0;
}

static void do_something(int connfd) {
  char rbuf[64] = {};
  ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
  if (n < 0) {
    msg("read() error");
    return;
  }

  printf("client says: %s\n", rbuf);
}

int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd < 0) {
    die("socket()");
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);                   // Port selection
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK); // 127.0.0.1
  int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

  if (rv) {
    die("connect");
  }

  char msg[] = "hello";
  write(fd, msg, strlen(msg));

  char rbuf[64] = {};
  ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
  if (n < 0) {
    die("read");
  }

  int32_t err = query(fd, "hello1");
  if (err) {
    goto L_DONE;
  }

  err = query(fd, "hello2");
  if (err) {
    goto L_DONE;
  }

  err = query(fd, "hello3");
  if (err) {
    goto L_DONE;
  }

L_DONE:
  close(fd);

  return 0;
}
