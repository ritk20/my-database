#pragma once

#include "avl.h"
#include "hashtable.h"
#include <string>

struct ZSet
{
  AVLNode *tree = nullptr;
  HMap hmap;
};

struct ZNode
{
  AVLNode tree;
  HNode hmap;
  double score = 0.0;
  std::string name;
};

bool zset_add(ZSet *zset, const std::string &name, double score);
ZNode *zset_lookup(ZSet *zset, const std::string &name);
ZNode *zset_pop(ZSet *zset, const std::string &name);
ZNode *zset_query(ZSet *zset, double score, const std::string &name);
void zset_dispose(ZSet *zset);
ZNode *znode_offset(ZNode *node, int64_t offset);
void znode_del(ZNode *node);
