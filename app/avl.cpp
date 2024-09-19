#include <stddef.h>
#include <stdint.h>
#include "avl.h"

// Initialize a node
void avl_init(AVLNode *node)
{
    node->depth = 1;
    node->count = 1;
    node->left = node->right = node->parent = nullptr;
}

// Get the depth of a node
uint32_t avl_depth(AVLNode *node)
{
    return node ? node->depth : 0;
}

// Get the number of nodes in the subtree rooted at node
uint32_t avl_count(AVLNode *node)
{
    return node ? node->count : 0;
}

// Helper to get the maximum of two values
uint32_t max(uint32_t lhs, uint32_t rhs)
{
    return (lhs > rhs) ? lhs : rhs;
}

// Update the depth and count of the node
void avl_update(AVLNode *node)
{
    node->depth = 1 + max(avl_depth(node->left), avl_depth(node->right));
    node->count = 1 + avl_count(node->left) + avl_count(node->right);
}

// Rotate node to the left
AVLNode *rotate_left(AVLNode *node)
{
    AVLNode *new_root = node->right;

    if (new_root->left)
    {
        new_root->left->parent = node;
    }

    node->right = new_root->left;
    new_root->left = node;

    new_root->parent = node->parent;
    node->parent = new_root;

    avl_update(node);
    avl_update(new_root);

    return new_root;
}

// Rotate node to the right
AVLNode *rotate_right(AVLNode *node)
{
    AVLNode *new_root = node->left;

    if (new_root->right)
    {
        new_root->right->parent = node;
    }

    node->left = new_root->right;
    new_root->right = node;

    new_root->parent = node->parent;
    node->parent = new_root;

    avl_update(node);
    avl_update(new_root);

    return new_root;
}

// Fix imbalance when the left subtree is too deep
AVLNode *avl_fix_left(AVLNode *root)
{
    if (avl_depth(root->left->left) < avl_depth(root->left->right))
    {
        root->left = rotate_left(root->left);
    }
    return rotate_right(root);
}

// Fix imbalance when the right subtree is too deep
AVLNode *avl_fix_right(AVLNode *root)
{
    if (avl_depth(root->right->right) < avl_depth(root->right->left))
    {
        root->right = rotate_right(root->right);
    }
    return rotate_left(root);
}

// Maintain balance by rotating nodes, propagating changes up to the root
AVLNode *avl_fix(AVLNode *node)
{
    while (true)
    {
        avl_update(node);

        uint32_t left_depth = avl_depth(node->left);
        uint32_t right_depth = avl_depth(node->right);

        AVLNode **parent_link = nullptr;
        if (node->parent)
        {
            parent_link = (node->parent->left == node) ? &node->parent->left : &node->parent->right;
        }

        if (left_depth == right_depth + 2)
        {
            node = avl_fix_left(node);
        }
        else if (right_depth == left_depth + 2)
        {
            node = avl_fix_right(node);
        }

        if (!parent_link)
        {
            return node; // Node is the root
        }

        *parent_link = node;
        node = node->parent;
    }
}

// Delete a node from the AVL tree and return the new root
AVLNode *avl_del(AVLNode *node)
{
    if (!node->right)
    {
        // No right subtree, link the left subtree to the parent
        AVLNode *parent = node->parent;

        if (node->left)
        {
            node->left->parent = parent;
        }

        if (parent)
        {
            // Attach the left child to the parent
            (parent->left == node ? parent->left : parent->right) = node->left;
            return avl_fix(parent);
        }
        else
        {
            // Deleting the root
            return node->left;
        }
    }
    else
    {
        // Swap node with its in-order successor (smallest node in the right subtree)
        AVLNode *successor = node->right;
        while (successor->left)
        {
            successor = successor->left;
        }

        AVLNode *new_root = avl_del(successor);

        *successor = *node;
        if (successor->left)
        {
            successor->left->parent = successor;
        }
        if (successor->right)
        {
            successor->right->parent = successor;
        }

        AVLNode *parent = node->parent;
        if (parent)
        {
            (parent->left == node ? parent->left : parent->right) = successor;
            return new_root;
        }
        else
        {
            // Deleting the root
            return successor;
        }
    }
}
