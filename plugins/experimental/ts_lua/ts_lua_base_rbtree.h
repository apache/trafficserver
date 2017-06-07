#ifndef _TS_RBTREE_H_INCLUDED_
#define _TS_RBTREE_H_INCLUDED_

typedef unsigned int ts_rbtree_key_t;
typedef int ts_rbtree_key_int_t;

typedef struct ts_rbtree_node_s ts_rbtree_node_t;

struct ts_rbtree_node_s {
  ts_rbtree_key_t key;
  ts_rbtree_node_t *left;
  ts_rbtree_node_t *right;
  ts_rbtree_node_t *parent;
  unsigned char color;
  unsigned char data;
};

typedef struct ts_rbtree_s ts_rbtree_t;

typedef void (*ts_rbtree_insert_pt)(ts_rbtree_node_t *root, ts_rbtree_node_t *node, ts_rbtree_node_t *sentinel);

struct ts_rbtree_s {
  ts_rbtree_node_t *root;
  ts_rbtree_node_t *sentinel;
  ts_rbtree_insert_pt insert;
};

#define ts_rbtree_init(tree, s, i) \
  ts_rbtree_sentinel_init(s);      \
  (tree)->root     = s;            \
  (tree)->sentinel = s;            \
  (tree)->insert   = i

void ts_rbtree_insert(ts_rbtree_t *tree, ts_rbtree_node_t *node);
void ts_rbtree_delete(ts_rbtree_t *tree, ts_rbtree_node_t *node);
// void ts_rbtree_insert_value(ts_rbtree_node_t *root, ts_rbtree_node_t *node,
//     ts_rbtree_node_t *sentinel);
// void ts_rbtree_insert_timer_value(ts_rbtree_node_t *root,
//     ts_rbtree_node_t *node, ts_rbtree_node_t *sentinel);

#define ts_rbt_red(node) ((node)->color = 1)
#define ts_rbt_black(node) ((node)->color = 0)
#define ts_rbt_is_red(node) ((node)->color)
#define ts_rbt_is_black(node) (!ts_rbt_is_red(node))
#define ts_rbt_copy_color(n1, n2) (n1->color = n2->color)

/* a sentinel must be black */

#define ts_rbtree_sentinel_init(node) ts_rbt_black(node)

static inline ts_rbtree_node_t *
ts_rbtree_min(ts_rbtree_node_t *node, ts_rbtree_node_t *sentinel)
{
  while (node->left != sentinel) {
    node = node->left;
  }

  return node;
}

#endif /* _TS_RBTREE_H_INCLUDED_ */
