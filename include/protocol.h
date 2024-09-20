#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <vector>
#include <string>
#include "common.h"

// Constants
constexpr size_t k_max_msg = 4096;

// State Definitions
enum
{
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2, // mark the connection for deletion
};

// Connection Structure
struct Conn;

// Function Declarations
bool try_one_request(Conn *conn);
bool try_fill_buffer(Conn *conn);
bool try_flush_buffer(Conn *conn);
void state_req(Conn *conn);
void state_res(Conn *conn);

#endif // PROTOCOL_H
