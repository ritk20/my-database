#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <cstddef>
#include "avl.h"

#define container_of(ptr, type, member) \
  (reinterpret_cast<type *>(            \
      reinterpret_cast<char *>(ptr) - offsetof(type, member)))

struct Data
{
  AVLNode node;
  uint32_t val = 0;
};

struct Container
{
  AVLNode *root = nullptr;
};

// Add a value to the AVL tree
static void add(Container &c, uint32_t val)
{
  Data *data = new Data(); // allocate the data
  avl_init(&data->node);
  data->val = val;

  AVLNode *cur = nullptr;   // current node
  AVLNode **from = &c.root; // pointer to the current node's child
  while (*from)
  { // tree search
    cur = *from;
    uint32_t node_val = container_of(cur, Data, node)->val;
    from = (val < node_val) ? &cur->left : &cur->right;
  }
  *from = &data->node; // attach the new node
  data->node.parent = cur;
  c.root = avl_fix(&data->node);
}

// Delete a value from the AVL tree
static bool del(Container &c, uint32_t val)
{
  AVLNode *cur = c.root;
  while (cur)
  {
    uint32_t node_val = container_of(cur, Data, node)->val;
    if (val == node_val)
    {
      break;
    }
    cur = (val < node_val) ? cur->left : cur->right;
  }
  if (!cur)
  {
    return false;
  }

  c.root = avl_del(cur);
  delete container_of(cur, Data, node);
  return true;
}

// Verify AVL tree invariants
static void avl_verify(AVLNode *parent, AVLNode *node)
{
  if (!node)
  {
    return;
  }

  assert(node->parent == parent);
  avl_verify(node, node->left);
  avl_verify(node, node->right);

  assert(node->count == 1 + avl_count(node->left) + avl_count(node->right));

  uint32_t l = avl_depth(node->left);
  uint32_t r = avl_depth(node->right);
  assert(l == r || l + 1 == r || l == r + 1);
  assert(node->depth == 1 + max(l, r));

  uint32_t val = container_of(node, Data, node)->val;
  if (node->left)
  {
    assert(node->left->parent == node);
    assert(container_of(node->left, Data, node)->val <= val);
  }
  if (node->right)
  {
    assert(node->right->parent == node);
    assert(container_of(node->right, Data, node)->val >= val);
  }
}

// Extract values from the AVL tree into a multiset
static void extract(AVLNode *node, std::multiset<uint32_t> &extracted)
{
  if (!node)
  {
    return;
  }
  extract(node->left, extracted);
  extracted.insert(container_of(node, Data, node)->val);
  extract(node->right, extracted);
}

// Verify the container against a reference multiset
static void container_verify(Container &c, const std::multiset<uint32_t> &ref)
{
  avl_verify(nullptr, c.root);
  assert(avl_count(c.root) == ref.size());
  std::multiset<uint32_t> extracted;
  extract(c.root, extracted);
  assert(extracted == ref);
}

// Dispose of the AVL tree and free memory
static void dispose(Container &c)
{
  while (c.root)
  {
    AVLNode *node = c.root;
    c.root = avl_del(c.root);
    delete container_of(node, Data, node);
  }
}

// Test inserting a single value
static void test_insert(uint32_t sz)
{
  for (uint32_t val = 0; val < sz; ++val)
  {
    Container c;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < sz; ++i)
    {
      if (i == val)
      {
        continue;
      }
      add(c, i);
      ref.insert(i);
    }
    container_verify(c, ref);

    add(c, val);
    ref.insert(val);
    container_verify(c, ref);
    dispose(c);
  }
  printf("test_insert(%u) passed\n", sz);
}

// Test inserting duplicate values
static void test_insert_dup(uint32_t sz)
{
  for (uint32_t val = 0; val < sz; ++val)
  {
    Container c;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < sz; ++i)
    {
      add(c, i);
      ref.insert(i);
    }
    container_verify(c, ref);

    add(c, val);
    ref.insert(val);
    container_verify(c, ref);
    dispose(c);
  }
  printf("test_insert_dup(%u) passed\n", sz);
}

// Test removing values
static void test_remove(uint32_t sz)
{
  for (uint32_t val = 0; val < sz; ++val)
  {
    Container c;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < sz; ++i)
    {
      add(c, i);
      ref.insert(i);
    }
    container_verify(c, ref);

    assert(del(c, val));
    ref.erase(val);
    container_verify(c, ref);
    dispose(c);
  }
  printf("test_remove(%u) passed\n", sz);
}

// Test inserting and removing random values
static void test_random_operations(uint32_t sz)
{
  Container c;
  std::multiset<uint32_t> ref;
  for (uint32_t i = 0; i < sz; ++i)
  {
    uint32_t val = static_cast<uint32_t>(rand()) % (sz * 2);
    add(c, val);
    ref.insert(val);
  }
  container_verify(c, ref);

  for (uint32_t i = 0; i < sz / 2; ++i)
  {
    uint32_t val = static_cast<uint32_t>(rand()) % (sz * 2);
    auto it = ref.find(val);
    if (it == ref.end())
    {
      assert(!del(c, val));
    }
    else
    {
      assert(del(c, val));
      ref.erase(it);
    }
    container_verify(c, ref);
  }
  dispose(c);
  printf("test_random_operations(%u) passed\n", sz);
}

int main()
{
  Container c;

  // Quick tests
  container_verify(c, {});
  add(c, 123);
  container_verify(c, {123});
  assert(!del(c, 124));
  assert(del(c, 123));
  container_verify(c, {});
  printf("Quick tests passed\n");

  // Sequential insertion
  std::multiset<uint32_t> ref;
  for (uint32_t i = 0; i < 1000; i += 3)
  {
    add(c, i);
    ref.insert(i);
    container_verify(c, ref);
  }
  printf("Sequential insertion passed\n");

  // Random insertion
  for (uint32_t i = 0; i < 100; i++)
  {
    uint32_t val = static_cast<uint32_t>(rand()) % 1000;
    add(c, val);
    ref.insert(val);
    container_verify(c, ref);
  }
  printf("Random insertion passed\n");

  // Random deletion
  for (uint32_t i = 0; i < 200; i++)
  {
    uint32_t val = static_cast<uint32_t>(rand()) % 1000;
    auto it = ref.find(val);
    if (it == ref.end())
    {
      assert(!del(c, val));
    }
    else
    {
      assert(del(c, val));
      ref.erase(it);
    }
    container_verify(c, ref);
  }
  printf("Random deletion passed\n");

  // Insertion/deletion at various positions
  for (uint32_t i = 0; i < 200; ++i)
  {
    test_insert(i);
    test_insert_dup(i);
    test_remove(i);
  }

  // Random operations
  test_random_operations(1000);

  dispose(c);
  return 0;
}
