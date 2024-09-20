#ifndef CONNECTION_H
#define CONNECTION_H

#include <winsock2.h>
#include <vector>
#include "protocol.h"
#include "common.h"

struct Conn
{
  SOCKET fd = INVALID_SOCKET;
  std::uint32_t state = 0; // either STATE_REQ or STATE_RES
  // buffer for reading
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  // buffer for writing
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

class ConnectionManager
{
public:
  ConnectionManager();
  ~ConnectionManager();
  void initialize();
  void run();

private:
  SOCKET listen_fd;
  std::vector<Conn *> fd2conn;
  void accept_new_conn();
  void handle_connection_io(Conn *conn);
  void cleanup_connection(Conn *conn);
};

#endif // CONNECTION_H
