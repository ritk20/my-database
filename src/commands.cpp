#include "commands.h"
#include "datastore.h"
#include "common.h"
#include <cstring>
#include <cassert>
#include <cstdint>

// Implement command handling functions

static bool cmd_is(const std::string &word, const char *cmd)
{
  return _stricmp(word.c_str(), cmd) == 0;
}

void do_request(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.empty())
  {
    out_err(out, ERR_ARG, "Empty command");
    return;
  }

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
  else if (cmd.size() == 4 && cmd_is(cmd[0], "zadd"))
  {
    do_zadd(cmd, out);
  }
  else if (cmd.size() == 3 && cmd_is(cmd[0], "zrem"))
  {
    do_zrem(cmd, out);
  }
  else if (cmd.size() == 3 && cmd_is(cmd[0], "zscore"))
  {
    do_zscore(cmd, out);
  }
  else if (cmd.size() == 6 && cmd_is(cmd[0], "zquery"))
  {
    do_zquery(cmd, out);
  }
  else
  {
    // cmd is not recognized
    out_err(out, ERR_UNKNOWN, "Unknown cmd");
  }
}
