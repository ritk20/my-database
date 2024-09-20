#include "protocol.h"
#include "commands.h"
#include "connection.h"
#include "datastore.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
// Assuming other necessary includes and using directives

const size_t k_max_args = 4096;

// Implementation of request parsing and response generation

static int32_t parse_req(const uint8_t *data, size_t len, std::vector<std::string> &out)
{
  if (len < 4)
  {
    return -1;
  }
  std::uint32_t n = 0;
  memcpy(&n, &data[0], 4);
  if (n > k_max_args)
  {
    return -1;
  }

  size_t pos = 4;
  while (n--)
  {
    if (pos + 4 > len)
    {
      return -1;
    }
    std::uint32_t sz = 0;
    memcpy(&sz, &data[pos], 4);
    if (pos + 4 + sz > len)
    {
      return -1;
    }
    out.emplace_back((char *)&data[pos + 4], sz);
    pos += 4 + sz;
  }

  if (pos != len)
  {
    return -1; // trailing garbage
  }
  return 0;
}

bool try_one_request(Conn *conn)
{
  // try to parse a request from the buffer
  if (conn->rbuf_size < 4)
  {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }
  std::uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], 4);
  if (len > k_max_msg)
  {
    msg("too long");
    conn->state = STATE_END;
    return false;
  }
  if (len + 4 > conn->rbuf_size)
  {
    // not enough data in the buffer. Will retry in the next iteration
    return false;
  }

  // parse the request
  std::vector<std::string> cmd;
  if (0 != parse_req(&conn->rbuf[4], len, cmd))
  {
    msg("bad req");
    conn->state = STATE_END;
    return false;
  }

  // got one request, generate the response.
  std::string out;
  do_request(cmd, out);

  // pack the response into the buffer
  if (4 + out.size() > k_max_msg)
  {
    out.clear();
    out_err(out, ERR_2BIG, "response is too big");
  }

  std::uint32_t wlen = (std::uint32_t)out.size();
  memcpy(&conn->wbuf[0], &wlen, 4);
  memcpy(&conn->wbuf[4], out.data(), out.size());
  conn->wbuf_size = 4 + wlen;

  size_t remain = conn->rbuf_size - 4 - len;
  if (remain)
  {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  // change state
  conn->state = STATE_RES;
  state_res(conn);

  // continue the outer loop if the request was fully processed
  return (conn->state == STATE_REQ);
}

bool try_fill_buffer(Conn *conn)
{
  // try to fill the buffer
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  int rv = 0;
  do
  {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = recv(conn->fd, (char *)&conn->rbuf[conn->rbuf_size], (int)cap, 0);
  } while (rv < 0 && WSAGetLastError() == WSAEINTR);
  if (rv < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
  {
    // got WSAEWOULDBLOCK, stop.
    return false;
  }
  if (rv < 0)
  {
    msg("recv() error");
    conn->state = STATE_END;
    return false;
  }
  if (rv == 0)
  {
    if (conn->rbuf_size > 0)
    {
      msg("unexpected EOF");
    }
    else
    {
      msg("EOF");
    }
    conn->state = STATE_END;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf));

  while (try_one_request(conn))
  {
    // Continue processing
  }
  return (conn->state == STATE_REQ);
}

void state_req(Conn *conn)
{
  while (try_fill_buffer(conn))
  {
    // Continue filling the buffer and processing requests
  }
}

bool try_flush_buffer(Conn *conn)
{
  int rv = 0;
  do
  {
    size_t remain = conn->wbuf_size - conn->wbuf_sent;
    rv = send(conn->fd, (char *)&conn->wbuf[conn->wbuf_sent], (int)remain, 0);
  } while (rv < 0 && WSAGetLastError() == WSAEINTR);
  if (rv < 0 && WSAGetLastError() == WSAEWOULDBLOCK)
  {
    // got WSAEWOULDBLOCK, stop.
    return false;
  }
  if (rv < 0)
  {
    msg("send() error");
    conn->state = STATE_END;
    return false;
  }
  conn->wbuf_sent += (size_t)rv;
  assert(conn->wbuf_sent <= conn->wbuf_size);
  if (conn->wbuf_sent == conn->wbuf_size)
  {
    // response was fully sent, change state back
    conn->state = STATE_REQ;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    return false;
  }
  // still got some data in wbuf, could try to write again
  return true;
}

void state_res(Conn *conn)
{
  while (try_flush_buffer(conn))
  {
    // Continue flushing the buffer
  }
}
