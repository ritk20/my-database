#ifndef AVL_H
#define AVL_H

#include <cstddef>
#include <cstdint>

struct AVLNode
{
  uint32_t depth = 0;
  uint32_t count = 0;
  AVLNode *left = nullptr;
  AVLNode *right = nullptr;
  AVLNode *parent = nullptr;
};

void avl_init(AVLNode *node);
uint32_t avl_depth(AVLNode *node);
uint32_t avl_count(AVLNode *node);
uint32_t max(uint32_t lhs, uint32_t rhs);
void avl_update(AVLNode *node);
AVLNode *rotate_left(AVLNode *node);
AVLNode *rotate_right(AVLNode *node);
AVLNode *avl_fix_left(AVLNode *root);
AVLNode *avl_fix_right(AVLNode *root);
AVLNode *avl_fix(AVLNode *node);
AVLNode *avl_del(AVLNode *node);

#endif
