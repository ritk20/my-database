#ifndef DATASTORE_H
#define DATASTORE_H

#include <string>
#include <vector>
#include "hashtable.h"
#include "zset.h"

// Data Store Structure
struct DataStore
{
  HMap db;
};

// External DataStore instance
extern DataStore g_data;

enum EntryType
{
  T_STR = 0,
  T_ZSET = 1,
};
// the structure for the key
struct Entry
{
  struct HNode node;
  std::string key;
  std::string val;
  std::uint32_t type = 0;
  ZSet *zset = NULL;
};

// Function Declarations for Commands
void do_get(std::vector<std::string> &cmd, std::string &out);
void do_set(std::vector<std::string> &cmd, std::string &out);
void do_del(std::vector<std::string> &cmd, std::string &out);
void do_keys(std::vector<std::string> &cmd, std::string &out);
void do_zadd(std::vector<std::string> &cmd, std::string &out);
void do_zrem(std::vector<std::string> &cmd, std::string &out);
void do_zscore(std::vector<std::string> &cmd, std::string &out);
void do_zquery(std::vector<std::string> &cmd, std::string &out);

// Utility Functions
bool str2dbl(const std::string &s, double &out);
bool str2int(const std::string &s, int64_t &out);
bool expect_zset(std::string &out, std::string &s, Entry **ent);

// Serialization Functions
void out_nil(std::string &out);
void out_str(std::string &out, const std::string &val);
void out_int(std::string &out, int64_t val);
void out_dbl(std::string &out, double val);
void out_err(std::string &out, int32_t code, const std::string &m);
void out_arr(std::string &out, std::uint32_t n);
size_t begin_arr(std::string &out);
void end_arr(std::string &out, size_t ctx, std::uint32_t n);

#endif // DATASTORE_H