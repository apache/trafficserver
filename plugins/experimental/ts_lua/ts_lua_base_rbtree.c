#include <stdio.h>
#include <stdlib.h>

#include "ts_lua_base_rbtree.h"

/*
 * The red-black tree code is based on the algorithm described in
 * the "Introduction to Algorithms" by Cormen, Leiserson and Rivest.
 */

static inline void ts_rbtree_left_rotate(ts_rbtree_node_t **root, ts_rbtree_node_t *sentinel, ts_rbtree_node_t *node);
static inline void ts_rbtree_right_rotate(ts_rbtree_node_t **root, ts_rbtree_node_t *sentinel, ts_rbtree_node_t *node);

void
ts_rbtree_insert(ts_rbtree_t *tree, ts_rbtree_node_t *node)
{
  ts_rbtree_node_t **root, *temp, *sentinel;

  /* a binary tree insert */

  root     = (ts_rbtree_node_t **)&tree->root;
  sentinel = tree->sentinel;

  if (*root == sentinel) {
    node->parent = NULL;
    node->left   = sentinel;
    node->right  = sentinel;
    ts_rbt_black(node);
    *root = node;

    return;
  }

  tree->insert(*root, node, sentinel);

  /* re-balance tree */

  while (node != *root && ts_rbt_is_red(node->parent)) {
    if (node->parent == node->parent->parent->left) {
      temp = node->parent->parent->right;

      if (ts_rbt_is_red(temp)) {
        ts_rbt_black(node->parent);
        ts_rbt_black(temp);
        ts_rbt_red(node->parent->parent);
        node = node->parent->parent;

      } else {
        if (node == node->parent->right) {
          node = node->parent;
          ts_rbtree_left_rotate(root, sentinel, node);
        }

        ts_rbt_black(node->parent);
        ts_rbt_red(node->parent->parent);
        ts_rbtree_right_rotate(root, sentinel, node->parent->parent);
      }

    } else {
      temp = node->parent->parent->left;

      if (ts_rbt_is_red(temp)) {
        ts_rbt_black(node->parent);
        ts_rbt_black(temp);
        ts_rbt_red(node->parent->parent);
        node = node->parent->parent;

      } else {
        if (node == node->parent->left) {
          node = node->parent;
          ts_rbtree_right_rotate(root, sentinel, node);
        }

        ts_rbt_black(node->parent);
        ts_rbt_red(node->parent->parent);
        ts_rbtree_left_rotate(root, sentinel, node->parent->parent);
      }
    }
  }

  ts_rbt_black(*root);
}

void
ts_rbtree_insert_value(ts_rbtree_node_t *temp, ts_rbtree_node_t *node, ts_rbtree_node_t *sentinel)
{
  ts_rbtree_node_t **p;

  for (;;) {
    p = (node->key < temp->key) ? &temp->left : &temp->right;

    if (*p == sentinel) {
      break;
    }

    temp = *p;
  }

  *p           = node;
  node->parent = temp;
  node->left   = sentinel;
  node->right  = sentinel;
  ts_rbt_red(node);
}

void
ts_rbtree_insert_timer_value(ts_rbtree_node_t *temp, ts_rbtree_node_t *node, ts_rbtree_node_t *sentinel)
{
  ts_rbtree_node_t **p;

  for (;;) {
    /*
     * Timer values
     * 1) are spread in small range, usually several minutes,
     * 2) and overflow each 49 days, if milliseconds are stored in 32 bits.
     * The comparison takes into account that overflow.
     */

    /*  node->key < temp->key */

    p = ((ts_rbtree_key_int_t)(node->key - temp->key) < 0) ? &temp->left : &temp->right;

    if (*p == sentinel) {
      break;
    }

    temp = *p;
  }

  *p           = node;
  node->parent = temp;
  node->left   = sentinel;
  node->right  = sentinel;
  ts_rbt_red(node);
}

void
ts_rbtree_delete(ts_rbtree_t *tree, ts_rbtree_node_t *node)
{
  unsigned int red;
  ts_rbtree_node_t **root, *sentinel, *subst, *temp, *w;

  /* a binary tree delete */

  root     = (ts_rbtree_node_t **)&tree->root;
  sentinel = tree->sentinel;

  if (node->left == sentinel) {
    temp  = node->right;
    subst = node;

  } else if (node->right == sentinel) {
    temp  = node->left;
    subst = node;

  } else {
    subst = ts_rbtree_min(node->right, sentinel);

    if (subst->left != sentinel) {
      temp = subst->left;
    } else {
      temp = subst->right;
    }
  }

  if (subst == *root) {
    *root = temp;
    ts_rbt_black(temp);

    /* DEBUG stuff */
    node->left   = NULL;
    node->right  = NULL;
    node->parent = NULL;
    node->key    = 0;

    return;
  }

  red = ts_rbt_is_red(subst);

  if (subst == subst->parent->left) {
    subst->parent->left = temp;

  } else {
    subst->parent->right = temp;
  }

  if (subst == node) {
    temp->parent = subst->parent;

  } else {
    if (subst->parent == node) {
      temp->parent = subst;

    } else {
      temp->parent = subst->parent;
    }

    subst->left   = node->left;
    subst->right  = node->right;
    subst->parent = node->parent;
    ts_rbt_copy_color(subst, node);

    if (node == *root) {
      *root = subst;

    } else {
      if (node == node->parent->left) {
        node->parent->left = subst;
      } else {
        node->parent->right = subst;
      }
    }

    if (subst->left != sentinel) {
      subst->left->parent = subst;
    }

    if (subst->right != sentinel) {
      subst->right->parent = subst;
    }
  }

  /* DEBUG stuff */
  node->left   = NULL;
  node->right  = NULL;
  node->parent = NULL;
  node->key    = 0;

  if (red) {
    return;
  }

  /* a delete fixup */

  while (temp != *root && ts_rbt_is_black(temp)) {
    if (temp == temp->parent->left) {
      w = temp->parent->right;

      if (ts_rbt_is_red(w)) {
        ts_rbt_black(w);
        ts_rbt_red(temp->parent);
        ts_rbtree_left_rotate(root, sentinel, temp->parent);
        w = temp->parent->right;
      }

      if (ts_rbt_is_black(w->left) && ts_rbt_is_black(w->right)) {
        ts_rbt_red(w);
        temp = temp->parent;

      } else {
        if (ts_rbt_is_black(w->right)) {
          ts_rbt_black(w->left);
          ts_rbt_red(w);
          ts_rbtree_right_rotate(root, sentinel, w);
          w = temp->parent->right;
        }

        ts_rbt_copy_color(w, temp->parent);
        ts_rbt_black(temp->parent);
        ts_rbt_black(w->right);
        ts_rbtree_left_rotate(root, sentinel, temp->parent);
        temp = *root;
      }

    } else {
      w = temp->parent->left;

      if (ts_rbt_is_red(w)) {
        ts_rbt_black(w);
        ts_rbt_red(temp->parent);
        ts_rbtree_right_rotate(root, sentinel, temp->parent);
        w = temp->parent->left;
      }

      if (ts_rbt_is_black(w->left) && ts_rbt_is_black(w->right)) {
        ts_rbt_red(w);
        temp = temp->parent;

      } else {
        if (ts_rbt_is_black(w->left)) {
          ts_rbt_black(w->right);
          ts_rbt_red(w);
          ts_rbtree_left_rotate(root, sentinel, w);
          w = temp->parent->left;
        }

        ts_rbt_copy_color(w, temp->parent);
        ts_rbt_black(temp->parent);
        ts_rbt_black(w->left);
        ts_rbtree_right_rotate(root, sentinel, temp->parent);
        temp = *root;
      }
    }
  }

  ts_rbt_black(temp);
}

static inline void
ts_rbtree_left_rotate(ts_rbtree_node_t **root, ts_rbtree_node_t *sentinel, ts_rbtree_node_t *node)
{
  ts_rbtree_node_t *temp;

  temp        = node->right;
  node->right = temp->left;

  if (temp->left != sentinel) {
    temp->left->parent = node;
  }

  temp->parent = node->parent;

  if (node == *root) {
    *root = temp;

  } else if (node == node->parent->left) {
    node->parent->left = temp;

  } else {
    node->parent->right = temp;
  }

  temp->left   = node;
  node->parent = temp;
}

static inline void
ts_rbtree_right_rotate(ts_rbtree_node_t **root, ts_rbtree_node_t *sentinel, ts_rbtree_node_t *node)
{
  ts_rbtree_node_t *temp;

  temp       = node->left;
  node->left = temp->right;

  if (temp->right != sentinel) {
    temp->right->parent = node;
  }

  temp->parent = node->parent;

  if (node == *root) {
    *root = temp;

  } else if (node == node->parent->right) {
    node->parent->right = temp;

  } else {
    node->parent->left = temp;
  }

  temp->right  = node;
  node->parent = temp;
}
