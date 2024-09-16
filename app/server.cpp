#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define ssize_t ptrdiff_t

#pragma comment(lib, "ws2_32.lib")

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg)
{
    int err = WSAGetLastError();
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

static void fd_set_nb(SOCKET fd)
{
    u_long mode = 1;
    int result = ioctlsocket(fd, FIONBIO, &mode);
    if (result != NO_ERROR)
    {
        die("ioctlsocket error");
    }
}

const size_t k_max_msg = 4096;

enum
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2, // mark the connection for deletion
};

struct Conn
{
    SOCKET fd = INVALID_SOCKET;
    uint32_t state = 0; // either STATE_REQ or STATE_RES
    // buffer for reading
    size_t rbuf_size = 0;
    uint8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    uint8_t wbuf[4 + k_max_msg];
};

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn)
{
    if (fd2conn.size() <= (size_t)conn->fd)
    {
        fd2conn.resize(conn->fd + 1);
    }
    fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, SOCKET fd)
{
    // accept
    struct sockaddr_in client_addr = {};
    int socklen = sizeof(client_addr);
    SOCKET connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd == INVALID_SOCKET)
    {
        msg("accept() error");
        return -1; // error
    }

    // set the new connection fd to nonblocking mode
    fd_set_nb(connfd);
    // creating the struct Conn
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if (!conn)
    {
        closesocket(connfd);
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

static void state_req(Conn *conn);
static void state_res(Conn *conn);

static bool try_one_request(Conn *conn)
{
    // try to parse a request from the buffer
    if (conn->rbuf_size < 4)
    {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    uint32_t len = 0;
    memcpy(&len, &conn->rbuf[0], 4);
    if (len > k_max_msg)
    {
        msg("too long");
        conn->state = STATE_END;
        return false;
    }
    if (4 + len > conn->rbuf_size)
    {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }

    // got one request, do something with it
    printf("client says: %.*s\n", len, &conn->rbuf[4]);

    // generating echoing response
    memcpy(&conn->wbuf[0], &len, 4);
    memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
    conn->wbuf_size = 4 + len;

    // remove the request from the buffer.
    // note: frequent memmove is inefficient.
    // note: need better handling for production code.
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

static bool try_fill_buffer(Conn *conn)
{
    // try to fill the buffer
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    int rv = 0;
    do
    {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = recv(conn->fd, (char *)&conn->rbuf[conn->rbuf_size], cap, 0);
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

    // Try to process requests one by one.
    // Why is there a loop? Please read the explanation of "pipelining".
    while (try_one_request(conn))
    {
    }
    return (conn->state == STATE_REQ);
}

static void state_req(Conn *conn)
{
    while (try_fill_buffer(conn))
    {
    }
}

static bool try_flush_buffer(Conn *conn)
{
    ssize_t rv = 0;
    do
    {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = send(conn->fd, (const char *)&conn->wbuf[conn->wbuf_sent], remain, 0);
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

static void state_res(Conn *conn)
{
    while (try_flush_buffer(conn))
    {
    }
}

static void connection_io(Conn *conn)
{
    if (conn->state == STATE_REQ)
    {
        state_req(conn);
    }
    else if (conn->state == STATE_RES)
    {
        state_res(conn);
    }
    else
    {
        assert(0); // not expected
    }
}

int main()
{
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0)
    {
        die("WSAStartup failed");
    }

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
    {
        die("socket()");
    }

    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&val, sizeof(val));

    // bind
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // wildcard address 0.0.0.0
    int rv = bind(fd, (const sockaddr *)&addr, sizeof(addr));
    if (rv)
    {
        die("bind()");
    }

    // listen
    rv = listen(fd, SOMAXCONN);
    if (rv)
    {
        die("listen()");
    }

    // a map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set the listen fd to nonblocking mode
    fd_set_nb(fd);

    // the event loop
    while (true)
    {
        fd_set readfds, writefds, exceptfds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_ZERO(&exceptfds);

        // Add the listening fd to the read set
        FD_SET(fd, &readfds);

        // Add connection fds to the appropriate sets
        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }
            if (conn->state == STATE_REQ)
            {
                FD_SET(conn->fd, &readfds);
            }
            else if (conn->state == STATE_RES)
            {
                FD_SET(conn->fd, &writefds);
            }
            FD_SET(conn->fd, &exceptfds);
        }

        // Use select instead of WSAPoll
        int rv = select(0, &readfds, &writefds, &exceptfds, NULL);
        if (rv == SOCKET_ERROR)
        {
            die("select failed");
        }

        // Check which file descriptors are ready
        if (FD_ISSET(fd, &readfds))
        {
            // Try to accept a new connection if the listening fd is active
            (void)accept_new_conn(fd2conn, fd);
        }

        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }
            if (FD_ISSET(conn->fd, &readfds) || FD_ISSET(conn->fd, &writefds) || FD_ISSET(conn->fd, &exceptfds))
            {
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    // client closed normally, or something bad happened.
                    // destroy this connection
                    fd2conn[conn->fd] = NULL;
                    closesocket(conn->fd);
                    free(conn);
                }
            }
        }
    }

    WSACleanup();
    return 0;
}
