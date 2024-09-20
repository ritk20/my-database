#include "connection.h"
#include "protocol.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Implement ConnectionManager methods

ConnectionManager::ConnectionManager() : listen_fd(INVALID_SOCKET) {}

ConnectionManager::~ConnectionManager()
{
  for (auto conn : fd2conn)
  {
    if (conn)
    {
      closesocket(conn->fd);
      free(conn);
    }
  }
  if (listen_fd != INVALID_SOCKET)
  {
    closesocket(listen_fd);
  }
  WSACleanup();
}

void ConnectionManager::initialize()
{
  WSADATA wsaData;
  int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0)
  {
    die("WSAStartup failed");
  }

  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == INVALID_SOCKET)
  {
    die("socket()");
  }

  int val = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

  // bind
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(INADDR_ANY); // wildcard address 0.0.0.0
  int rv = bind(listen_fd, (const sockaddr *)&addr, sizeof(addr));
  if (rv == SOCKET_ERROR)
  {
    die("bind()");
  }

  // listen
  rv = listen(listen_fd, SOMAXCONN);
  if (rv == SOCKET_ERROR)
  {
    die("listen()");
  }

  // set the listen fd to nonblocking mode
  fd_set_nb(listen_fd);
}

void ConnectionManager::run()
{
  fd_set read_fds, write_fds, except_fds;
  int max_fd = listen_fd;

  while (true)
  {
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);

    FD_SET(listen_fd, &read_fds);
    FD_SET(listen_fd, &except_fds);

    for (Conn *conn : fd2conn)
    {
      if (!conn)
        continue;
      if (conn->state == STATE_REQ)
      {
        FD_SET(conn->fd, &read_fds);
      }
      else if (conn->state == STATE_RES)
      {
        FD_SET(conn->fd, &write_fds);
      }
      FD_SET(conn->fd, &except_fds);
      if (conn->fd > max_fd)
      {
        max_fd = conn->fd;
      }
    }

    int rv = select(max_fd + 1, &read_fds, &write_fds, &except_fds, NULL);
    if (rv == SOCKET_ERROR)
    {
      die("select");
    }

    if (FD_ISSET(listen_fd, &read_fds))
    {
      accept_new_conn();
    }

    for (Conn *conn : fd2conn)
    {
      if (!conn)
        continue;
      if (FD_ISSET(conn->fd, &read_fds) ||
          FD_ISSET(conn->fd, &write_fds) ||
          FD_ISSET(conn->fd, &except_fds))
      {
        handle_connection_io(conn);
        if (conn->state == STATE_END)
        {
          cleanup_connection(conn);
        }
      }
    }
  }
}

void ConnectionManager::accept_new_conn()
{
  struct sockaddr_in client_addr = {};
  int socklen = sizeof(client_addr);
  SOCKET connfd = accept(listen_fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd == INVALID_SOCKET)
  {
    msg("accept() error");
    return;
  }

  // set the new connection fd to nonblocking mode
  fd_set_nb(connfd);
  // creating the struct Conn
  Conn *conn = (Conn *)malloc(sizeof(Conn));
  if (!conn)
  {
    closesocket(connfd);
    return;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_size = 0;
  conn->wbuf_sent = 0;

  // Ensure fd2conn can hold the new fd
  if (fd2conn.size() <= (size_t)conn->fd)
  {
    fd2conn.resize(conn->fd + 1, nullptr);
  }
  fd2conn[conn->fd] = conn;
}

void ConnectionManager::handle_connection_io(Conn *conn)
{
  if (conn->state == STATE_REQ)
  {
    state_req(conn);
  }
  else if (conn->state == STATE_RES)
  {
    state_res(conn);
  }
}

void ConnectionManager::cleanup_connection(Conn *conn)
{
  fd2conn[conn->fd] = nullptr;
  closesocket(conn->fd);
  free(conn);
}
