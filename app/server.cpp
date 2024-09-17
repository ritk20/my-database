#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
// #include <map>
#include "hashtable.h"

#pragma comment(lib, "Ws2_32.lib")

#define container_of(ptr, type, member) \
    (reinterpret_cast<type *>(          \
        reinterpret_cast<char *>(ptr) - offsetof(type, member)))

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

const size_t k_max_args = 1024;

static int32_t parse_req(
    const uint8_t *data, size_t len, std::vector<std::string> &out)
{
    if (len < 4)
    {
        return -1;
    }
    uint32_t n = 0;
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
        uint32_t sz = 0;
        memcpy(&sz, &data[pos], 4);
        if (pos + 4 + sz > len)
        {
            return -1;
        }
        out.push_back(std::string((char *)&data[pos + 4], sz));
        pos += 4 + sz;
    }

    if (pos != len)
    {
        return -1; // trailing garbage
    }
    return 0;
}

// static std::map<std::string, std::string> g_map;
// The data structure for the key space.
static struct
{
    HMap db;
} g_data;

// the structure for the key
struct Entry
{
    struct HNode node;
    std::string key;
    std::string val;
};

static bool entry_eq(HNode *lhs, HNode *rhs)
{
    struct Entry *le = container_of(lhs, struct Entry, node);
    struct Entry *re = container_of(rhs, struct Entry, node);
    return le->key == re->key;
}

static uint64_t str_hash(const uint8_t *data, size_t len)
{
    uint32_t h = 0x811C9DC5;
    for (size_t i = 0; i < len; i++)
    {
        h = (h + data[i]) * 0x01000193;
    }
    return h;
}

enum
{
    ERR_UNKNOWN = 1,
    ERR_2BIG = 2,
};

enum
{
    SER_NIL = 0,
    SER_ERR = 1,
    SER_STR = 2,
    SER_INT = 3,
    SER_ARR = 4,
};

static void out_nil(std::string &out)
{
    out.push_back(SER_NIL);
}

static void out_str(std::string &out, const std::string &val)
{
    out.push_back(SER_STR);
    uint32_t len = (uint32_t)val.size();
    out.append((char *)&len, 4);
    out.append(val);
}

static void out_int(std::string &out, int64_t val)
{
    out.push_back(SER_INT);
    out.append((char *)&val, 8);
}

static void out_err(std::string &out, int32_t code, const std::string &msg)
{
    out.push_back(SER_ERR);
    out.append((char *)&code, 4);
    uint32_t len = (uint32_t)msg.size();
    out.append((char *)&len, 4);
    out.append(msg);
}

static void out_arr(std::string &out, uint32_t n)
{
    out.push_back(SER_ARR);
    out.append((char *)&n, 4);
}

static void do_get(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (!node)
    {
        return out_nil(out);
    }

    const std::string &val = container_of(node, Entry, node)->val;
    out_str(out, val);
}

static void do_set(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        container_of(node, Entry, node)->val.swap(cmd[2]);
    }
    else
    {
        Entry *ent = new Entry();
        ent->key.swap(key.key);
        ent->node.hcode = key.node.hcode;
        ent->val.swap(cmd[2]);
        hm_insert(&g_data.db, &ent->node);
    }
    return out_nil(out);
}

static void do_del(std::vector<std::string> &cmd, std::string &out)
{
    Entry key;
    key.key.swap(cmd[1]);
    key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

    HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
    if (node)
    {
        delete container_of(node, Entry, node);
    }
    return out_int(out, node ? 1 : 0);
}

static void h_scan(HTab *tab, void (*f)(HNode *, void *), void *arg)
{
    if (tab->size == 0)
    {
        return;
    }
    for (size_t i = 0; i < tab->mask + 1; ++i)
    {
        HNode *node = tab->tab[i];
        while (node)
        {
            f(node, arg);
            node = node->next;
        }
    }
}

static void cb_scan(HNode *node, void *arg)
{
    std::string &out = *(std::string *)arg;
    out_str(out, container_of(node, Entry, node)->key);
}

static void do_keys(std::vector<std::string> &cmd, std::string &out)
{
    (void)cmd;
    out_arr(out, (uint32_t)hm_size(&g_data.db));
    h_scan(&g_data.db.ht1, &cb_scan, &out);
    h_scan(&g_data.db.ht2, &cb_scan, &out);
}

static bool cmd_is(const std::string &word, const char *cmd)
{
    return _stricmp(word.c_str(), cmd) == 0;
}
static void do_request(std::vector<std::string> &cmd, std::string &out)
{
    if (cmd.size() == 1 && cmd_is(cmd[0], "keys"))
    {
        do_keys(cmd, out);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "get"))
    {
        do_get(cmd, out);
    }
    else if (cmd.size() == 3 && cmd_is(cmd[0], "set"))
    {
        do_set(cmd, out);
    }
    else if (cmd.size() == 2 && cmd_is(cmd[0], "del"))
    {
        do_del(cmd, out);
    }
    else
    {
        // cmd is not recognized
        out_err(out, ERR_UNKNOWN, "Unknown cmd");
    }
}

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
    if (len + 4 > conn->rbuf_size)
    {
        // not enough data in the buffer. Will retry in the next iteration
        return false;
    }
    // parse the request
    std::vector<std::string> cmd;
    if (parse_req(&conn->rbuf[4], len, cmd) != 0)
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

    uint32_t wlen = (uint32_t)out.size();
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

static bool try_fill_buffer(Conn *conn)
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
    if (rv == SOCKET_ERROR)
    {
        die("bind()");
    }

    // listen
    rv = listen(fd, SOMAXCONN);
    if (rv == SOCKET_ERROR)
    {
        die("listen()");
    }

    // a map of all client connections, keyed by fd
    std::vector<Conn *> fd2conn;

    // set the listen fd to nonblocking mode
    fd_set_nb(fd);

    fd_set read_fds, write_fds, except_fds;
    int max_fd = fd;

    while (true)
    {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);

        FD_SET(fd, &read_fds);
        FD_SET(fd, &except_fds);

        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }
            FD_SET(conn->fd, (conn->state == STATE_REQ) ? &read_fds : &write_fds);
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

        if (FD_ISSET(fd, &read_fds))
        {
            (void)accept_new_conn(fd2conn, fd);
        }

        for (Conn *conn : fd2conn)
        {
            if (!conn)
            {
                continue;
            }
            if (FD_ISSET(conn->fd, &read_fds) || FD_ISSET(conn->fd, &write_fds) || FD_ISSET(conn->fd, &except_fds))
            {
                connection_io(conn);
                if (conn->state == STATE_END)
                {
                    fd2conn[conn->fd] = NULL;
                    (void)closesocket(conn->fd);
                    free(conn);
                }
            }
        }
    }

    WSACleanup();
    return 0;
}
