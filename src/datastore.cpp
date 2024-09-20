#include "datastore.h"
#include "common.h"
#include <math.h>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <iostream>
// Initialize the global data store
DataStore g_data;

// Implement command functions

static bool entry_eq(HNode *lhs, HNode *rhs)
{
  struct Entry *le = container_of(lhs, struct Entry, node);
  struct Entry *re = container_of(rhs, struct Entry, node);
  return le->key == re->key;
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

void do_get(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.size() != 2)
  {
    out_err(out, ERR_ARG, "GET requires 1 argument");
    return;
  }

  Entry key;
  key.key = cmd[1];
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!node)
  {
    return out_nil(out);
  }

  Entry *ent = container_of(node, Entry, node);
  if (ent->type != T_STR)
  {
    return out_err(out, ERR_TYPE, "expect string type");
  }
  return out_str(out, ent->val);
}

void do_set(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.size() != 3)
  {
    out_err(out, ERR_ARG, "SET requires 2 arguments");
    return;
  }

  Entry key;
  key.key = cmd[1];
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (node)
  {
    Entry *ent = container_of(node, Entry, node);
    if (ent->type != T_STR)
    {
      return out_err(out, ERR_TYPE, "expect string type");
    }
    ent->val = cmd[2];
  }
  else
  {
    Entry *ent = new Entry();
    ent->key = cmd[1];
    ent->node.hcode = key.node.hcode;
    ent->val = cmd[2];
    ent->type = T_STR;
    hm_insert(&g_data.db, &ent->node);
  }
  return out_nil(out);
}

void do_del(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.size() != 2)
  {
    out_err(out, ERR_ARG, "DEL requires 1 argument");
    return;
  }

  Entry key;
  key.key = cmd[1];
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = hm_pop(&g_data.db, &key.node, &entry_eq);
  if (node)
  {
    Entry *ent = container_of(node, Entry, node);
    // Clean up based on type
    switch (ent->type)
    {
    case T_ZSET:
      zset_dispose(ent->zset);
      delete ent->zset;
      break;
    case T_STR:
      // No additional cleanup needed
      break;
    default:
      break;
    }
    delete ent;
  }
  return out_int(out, node ? 1 : 0);
}

void do_keys(std::vector<std::string> &cmd, std::string &out)
{
  (void)cmd;
  out_arr(out, (std::uint32_t)hm_size(&g_data.db));
  h_scan(&g_data.db.ht1, &cb_scan, &out);
  h_scan(&g_data.db.ht2, &cb_scan, &out);
}

void do_zadd(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.size() != 4)
  {
    out_err(out, ERR_ARG, "ZADD requires 3 arguments");
    return;
  }

  double score = 0;
  if (!str2dbl(cmd[2], score))
  {
    return out_err(out, ERR_ARG, "expect floating-point number for score");
  }

  Entry key;
  key.key = cmd[1];
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());

  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);
  Entry *ent = nullptr;

  if (!hnode)
  {
    ent = new Entry();
    ent->key = cmd[1];
    ent->node.hcode = key.node.hcode;
    ent->type = T_ZSET;
    ent->zset = new ZSet();
    hm_insert(&g_data.db, &ent->node);
  }
  else
  {
    ent = container_of(hnode, Entry, node);
    if (ent->type != T_ZSET)
    {
      return out_err(out, ERR_TYPE, "expect zset");
    }
  }

  const std::string &name = cmd[3];
  bool added = zset_add(ent->zset, name, score);
  return out_int(out, (int64_t)added);
}

void do_zrem(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.size() != 3)
  {
    out_err(out, ERR_ARG, "ZREM requires 2 arguments");
    return;
  }

  Entry *ent = nullptr;
  if (!expect_zset(out, cmd[1], &ent))
  {
    return;
  }

  const std::string &name = cmd[2];
  ZNode *znode = zset_pop(ent->zset, name);
  if (znode)
  {
    znode_del(znode);
  }
  return out_int(out, znode ? 1 : 0);
}

void do_zscore(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.size() != 3)
  {
    out_err(out, ERR_ARG, "ZSCORE requires 2 arguments");
    return;
  }

  Entry *ent = nullptr;
  if (!expect_zset(out, cmd[1], &ent))
  {
    return;
  }

  const std::string &name = cmd[2];
  ZNode *znode = zset_lookup(ent->zset, name);
  return znode ? out_dbl(out, znode->score) : out_nil(out);
}

void do_zquery(std::vector<std::string> &cmd, std::string &out)
{
  if (cmd.size() != 6)
  {
    out_err(out, ERR_ARG, "ZQUERY requires 5 arguments");
    return;
  }

  double score = 0;
  if (!str2dbl(cmd[2], score))
  {
    return out_err(out, ERR_ARG, "expect floating-point number for score");
  }
  const std::string &name = cmd[3];
  int64_t offset = 0;
  int64_t limit = 0;
  if (!str2int(cmd[4], offset))
  {
    return out_err(out, ERR_ARG, "expect integer for offset");
  }
  if (!str2int(cmd[5], limit))
  {
    return out_err(out, ERR_ARG, "expect integer for limit");
  }

  Entry *ent = nullptr;
  if (!expect_zset(out, cmd[1], &ent))
  {
    if (out[0] == SER_NIL)
    {
      out.clear();
      out_arr(out, 0);
    }
    return;
  }

  if (limit <= 0)
  {
    return out_arr(out, 0);
  }

  ZNode *znode = zset_query(ent->zset, score, name);
  znode = znode_offset(znode, offset);

  std::cout << "Command size: " << cmd.size() << std::endl;
  std::cout << "Score: " << score << std::endl;
  std::cout << "Offset: " << offset << ", Limit: " << limit << std::endl;
  std::cout << "Expecting ZSet for key: " << cmd[1] << std::endl;
  std::cout << "Starting node: " << (znode ? znode->name : "nullptr") << std::endl;

  size_t arr_pos = begin_arr(out);
  uint32_t n = 0;
  while (znode && static_cast<int64_t>(n) < limit)
  {
    out_str(out, znode->name);
    out_dbl(out, znode->score);
    znode = znode_offset(znode, +1);
    n += 2;
  }
  end_arr(out, arr_pos, n);
}

// Utility Functions Implementation

bool str2dbl(const std::string &s, double &out)
{
  char *endp = nullptr;
  out = strtod(s.c_str(), &endp);
  return endp == s.c_str() + s.size() && !isnan(out);
}

bool str2int(const std::string &s, int64_t &out)
{
  char *endp = nullptr;
  out = strtoll(s.c_str(), &endp, 10);
  return endp == s.c_str() + s.size();
}

bool expect_zset(std::string &out, std::string &s, Entry **ent)
{
  Entry key;
  key.key = s;
  key.node.hcode = str_hash((uint8_t *)key.key.data(), key.key.size());
  HNode *hnode = hm_lookup(&g_data.db, &key.node, &entry_eq);
  if (!hnode)
  {
    out_nil(out);
    return false;
  }

  *ent = container_of(hnode, Entry, node);
  if ((*ent)->type != T_ZSET)
  {
    out_err(out, ERR_TYPE, "expect zset");
    return false;
  }
  return true;
}

void out_nil(std::string &out)
{
  out.push_back(SER_NIL);
}

void out_str(std::string &out, const std::string &val)
{
  out.push_back(SER_STR);
  std::uint32_t len = (std::uint32_t)val.size();
  out.append((char *)&len, 4);
  out.append(val);
}

void out_int(std::string &out, int64_t val)
{
  out.push_back(SER_INT);
  out.append((char *)&val, 8);
}

void out_dbl(std::string &out, double val)
{
  out.push_back(SER_DBL);
  out.append((char *)&val, 8);
}

void out_err(std::string &out, int32_t code, const std::string &m)
{
  out.push_back(SER_ERR);
  out.append((char *)&code, 4);
  std::uint32_t len = (std::uint32_t)m.size();
  out.append((char *)&len, 4);
  out.append(m);
}

void out_arr(std::string &out, std::uint32_t n)
{
  out.push_back(SER_ARR);
  out.append((char *)&n, 4);
}

size_t begin_arr(std::string &out)
{
  out.push_back(SER_ARR);
  out.append("\0\0\0\0", 4); // Placeholder for array length
  return out.size() - 4;     // Return position as size_t
}

void end_arr(std::string &out, size_t ctx, std::uint32_t n)
{
  size_t pos = ctx;
  assert(out[pos - 1] == SER_ARR);
  memcpy(&out[pos], &n, 4);
}
