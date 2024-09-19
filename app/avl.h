#ifndef AVL_H
#define AVL_H

#include <cstddef>
#include <cstdint>

struct AVLNode
{
  std::uint32_t depth = 0;
  std::uint32_t count = 0;
  AVLNode *left = nullptr;
  AVLNode *right = nullptr;
  AVLNode *parent = nullptr;
};

void avl_init(AVLNode *node);
void avl_update(AVLNode *node);
AVLNode *rotate_left(AVLNode *node);
AVLNode *rotate_right(AVLNode *node);
AVLNode *avl_fix_left(AVLNode *root);
AVLNode *avl_fix_right(AVLNode *root);
AVLNode *avl_fix(AVLNode *node);
AVLNode *avl_del(AVLNode *node);
AVLNode *avl_offset(AVLNode *node, int64_t offset);

#endif
