#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <winsock2.h>
#include "common.h"

void msg(const char *message)
{
  fprintf(stderr, "%s\n", message);
}

void die(const char *message)
{
  int err = WSAGetLastError();
  fprintf(stderr, "[%d] %s\n", err, message);
  abort();
}

void fd_set_nb(SOCKET fd)
{
  u_long mode = 1;
  int result = ioctlsocket(fd, FIONBIO, &mode);
  if (result != NO_ERROR)
  {
    die("ioctlsocket error");
  }
}
