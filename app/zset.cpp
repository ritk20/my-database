#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include "zset.h"
#include "common.h"

static ZNode *znode_new(const std::string &name, double score)
{
  ZNode *node = new ZNode();

  if (!node)
  {
    return nullptr;
  }

  avl_init(&node->tree);
  node->hmap.next = nullptr;
  node->hmap.hcode = str_hash(reinterpret_cast<const uint8_t *>(name.c_str()), name.size());
  node->score = score;
  node->name = name;
  return node;
}

static uint32_t min(size_t lhs, size_t rhs)
{
  return lhs < rhs ? lhs : rhs;
}

// compare by the (score, name) tuple
static bool zless(
    AVLNode *lhs, double score, const std::string &name)
{
  ZNode *zl = container_of(lhs, ZNode, tree);
  if (zl->score != score)
  {
    return zl->score < score;
  }
  int rv = memcmp(zl->name.c_str(), name.c_str(), min(zl->name.size(), name.size()));
  if (rv != 0)
  {
    return rv < 0;
  }
  return zl->name.size() < name.size();
}

static bool zless(AVLNode *lhs, AVLNode *rhs)
{
  ZNode *zr = container_of(rhs, ZNode, tree);
  return zless(lhs, zr->score, zr->name);
}

// insert into the AVL tree
static void tree_add(ZSet *zset, ZNode *node)
{
  AVLNode *cur = NULL;          // current node
  AVLNode **from = &zset->tree; // the incoming pointer to the next node
  while (*from)
  { // tree search
    cur = *from;
    from = zless(&node->tree, cur) ? &cur->left : &cur->right;
  }
  *from = &node->tree; // attach the new node
  node->tree.parent = cur;
  zset->tree = avl_fix(&node->tree);
}

// update the score of an existing node (AVL tree reinsertion)
static void zset_update(ZSet *zset, ZNode *node, double score)
{
  if (node->score == score)
  {
    return;
  }
  zset->tree = avl_del(&node->tree);
  node->score = score;
  avl_init(&node->tree);
  tree_add(zset, node);
}

// add a new (score, name) tuple, or update the score of the existing tuple
bool zset_add(ZSet *zset, const std::string &name, double score)
{
  ZNode *node = zset_lookup(zset, name);
  if (node)
  {
    zset_update(zset, node, score);
    return false;
  }
  else
  {
    node = znode_new(name, score);
    hm_insert(&zset->hmap, &node->hmap);
    tree_add(zset, node);
    return true;
  }
}

// a helper structure for the hashtable lookup
struct HKey
{
  HNode node;
  std::string name;
};

static bool hcmp(HNode *node, HNode *key)
{
  ZNode *znode = container_of(node, ZNode, hmap);
  HKey *hkey = container_of(key, HKey, node);
  if (znode->name.size() != hkey->name.size())
  {
    return false;
  }
  return 0 == memcmp(znode->name.c_str(), hkey->name.c_str(), znode->name.size());
}

// lookup by name
ZNode *zset_lookup(ZSet *zset, const std::string &name)
{
  if (!zset->tree)
  {
    return NULL;
  }

  HKey key;
  key.node.hcode = str_hash((uint8_t *)name.c_str(), name.size());
  key.name = name;
  HNode *found = hm_lookup(&zset->hmap, &key.node, &hcmp);
  return found ? container_of(found, ZNode, hmap) : NULL;
}

// deletion by name
ZNode *zset_pop(ZSet *zset, const std::string &name)
{
  if (!zset->tree)
  {
    return NULL;
  }

  HKey key;
  key.node.hcode = str_hash((uint8_t *)name.c_str(), name.size());
  key.name = name;
  HNode *found = hm_pop(&zset->hmap, &key.node, &hcmp);
  if (!found)
  {
    return NULL;
  }

  ZNode *node = container_of(found, ZNode, hmap);
  zset->tree = avl_del(&node->tree);
  return node;
}

// find the (score, name) tuple that is greater or equal to the argument.
ZNode *zset_query(ZSet *zset, double score, const std::string &name)
{
  AVLNode *found = NULL;
  AVLNode *cur = zset->tree;
  while (cur)
  {
    if (zless(cur, score, name))
    {
      cur = cur->right;
    }
    else
    {
      found = cur; // candidate
      cur = cur->left;
    }
  }
  return found ? container_of(found, ZNode, tree) : NULL;
}

// offset into the succeeding or preceding node.
ZNode *znode_offset(ZNode *node, int64_t offset)
{
  AVLNode *tnode = node ? avl_offset(&node->tree, offset) : NULL;
  return tnode ? container_of(tnode, ZNode, tree) : NULL;
}

void znode_del(ZNode *node)
{
  delete node;
}

static void tree_dispose(AVLNode *node)
{
  if (!node)
  {
    return;
  }
  tree_dispose(node->left);
  tree_dispose(node->right);
  znode_del(container_of(node, ZNode, tree));
}

// destroy the zset
void zset_dispose(ZSet *zset)
{
  tree_dispose(zset->tree);
  hm_destroy(&zset->hmap);
}
