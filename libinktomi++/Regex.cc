/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "inktomi++.h"
#include <fcntl.h>
#include "Bitops.h"
#include "Regex.h"

enum RENodeType
{
  RE_NODE_END,
  RE_NODE_EPSILON,
  RE_NODE_CCLASS,
  RE_NODE_CAT,
  RE_NODE_OR,
  RE_NODE_STAR,
  RE_NODE_PAREN,
  RE_NODE_STRING
};


typedef struct RENode RENode;
typedef struct RENodes RENodes;
typedef struct REState REState;
typedef struct RETransition RETransition;


struct RENode
{
  RENodeType type;
  int id;

  RENode **follow_nodes;
  RENode **first_nodes;
  RENode **last_nodes;

  union
  {
    int accept_num;
    unsigned char cclass[33];

    struct
    {
      RENode *left;
      RENode *right;
    } cat;

    struct
    {
      RENode *left;
      RENode *right;
    } _or;

    struct
    {
      RENode *child;
    } star;
    struct
    {
      RENode *child;
    } paren;
    struct
    {
      RENode *child;
    } string;
  } u;

  RENode *next;
};


struct RENodes
{
  RENodes *next;
  int size;
  RENode *nodes[1024];
};


struct REState
{
  intptr_t id;
  int accept_num;
  bool marked;
  unsigned char cclass[33];
  RETransition *transitions;
  RENode **nodes;
  REState *next;
};


struct RETransition
{
  REState *state;
  RETransition *next;
  RETransition *next_free;
  unsigned char cclass[33];
};


static void re_free();

static RENode *re_parse(const char *&pattern,
                        const char *end = NULL, REFlags flags = (REFlags) 0, int default_end_value = 0);
static unsigned char re_parse_char(const char *&buf, const char *end);
static RENode *node_alloc(RENodeType type);
static RENode **nodes_alloc(int n);
static RENode *re_end_node(int accept_num);
static RENode *re_epsilon_node();
static RENode *re_char_node(const char *&buf, const char *end, REFlags flags);
static RENode *re_cclass_node(const char *&buf, const char *end, REFlags flags);
static RENode *re_cat_node(RENode * left, RENode * right);
static RENode *re_or_node(RENode * left, RENode * right);
static RENode *re_star_node(RENode * child);
static RENode *re_paren_node(RENode * child);
static RENode *re_string_node(RENode * child);
static RENode *re_dup_node(RENode * n);
static void re_compute(RENode * n);
static bool re_nullable(RENode * n);
static RENode **re_first_nodes(RENode * n);
static RENode **re_last_nodes(RENode * n);
static RENode **re_union_nodes(RENode ** a, RENode ** b);
static bool re_equal_nodes(RENode ** a, RENode ** b);
static void re_print_node(RENode * n);
static void re_print_state(REState * s);
static void re_print_char(unsigned char c);
static void re_print_cclass(unsigned char *cclass);
static void re_print_nodes(RENode ** nodes);
static void re_debug(RENode * n);

static REState **re_construct(RENode * n, int *count);
static REState *state_alloc(RENode ** nodes);
static RETransition *transition_alloc(int input, REState * state);
static void transition_add(REState * state, RETransition * t);
static void state_clear(REState * state);

static void re_build(REState ** states,
                     int nstates,
                     DynArray<int>&base, DynArray<int>&accept, DynArray<int>&next, DynArray<int>&check);


static RENode *node_alloc_list = NULL;
static RENode *node_free_list = NULL;
static RENodes *nodes_alloc_list = NULL;
static RENodes *nodes_free_list = NULL;
static int nodes_pos = 0;
static int next_node_id = 1;

static REState *state_alloc_list = NULL;
static REState *state_free_list = NULL;
static RETransition *transition_alloc_list = NULL;
static RETransition *transition_free_list = NULL;
static int next_state_id = 0;



#ifdef TS_MICRO

static void
re_free()
{
  next_node_id = 1;
  next_state_id = 0;

  if (node_alloc_list) {
    ink_debug_assert(node_free_list == NULL);
    RENode *freeme, *last = node_alloc_list;
    while (last) {
      freeme = last;
      last = last->next;
      delete freeme;
    }
    node_alloc_list = NULL;
  }

  if (nodes_alloc_list) {
    RENodes *freeme, *last = nodes_alloc_list;
    while (last) {
      freeme = last;
      last = last->next;
      delete freeme;
    }
    nodes_alloc_list = NULL;
    last = nodes_free_list;
    while (last) {
      freeme = last;
      last = last->next;
      delete freeme;
    }
    nodes_free_list = NULL;
  }

  if (state_alloc_list) {
    ink_debug_assert(state_free_list == NULL);
    REState *freeme, *last = state_alloc_list;
    while (last) {
      freeme = last;
      last = last->next;
      delete freeme;
    }
    state_alloc_list = NULL;
  }

  if (transition_alloc_list) {
    ink_debug_assert(transition_free_list == NULL);
    RETransition *freeme, *last = transition_alloc_list;
    while (last) {
      freeme = last;
      last = last->next_free;
      delete freeme;
    }
    transition_alloc_list = NULL;
  }
}

#else

static void
re_free()
{
  next_node_id = 1;
  next_state_id = 0;

  if (node_alloc_list) {
    RENode *last = node_alloc_list;

    while (last->next)
      last = last->next;
    last->next = node_free_list;

    node_free_list = node_alloc_list;
    node_alloc_list = NULL;
  }

  if (nodes_alloc_list) {
    RENodes *last = nodes_alloc_list;

    while (last->next)
      last = last->next;
    last->next = nodes_free_list;

    nodes_free_list = nodes_alloc_list;
    nodes_alloc_list = NULL;
  }

  if (state_alloc_list) {
    REState *last = state_alloc_list;

    while (last->next)
      last = last->next;
    last->next = state_free_list;

    state_free_list = state_alloc_list;
    state_alloc_list = NULL;
  }

  if (transition_alloc_list) {
    RETransition *last = transition_alloc_list;

    while (last->next_free)
      last = last->next_free;
    last->next_free = transition_free_list;

    transition_free_list = transition_alloc_list;
    transition_alloc_list = NULL;
  }
}

#endif



static RENode *
re_parse(const char *&pattern, const char *end, REFlags flags, int default_end_value)
{
  static int recurse = 0;

  const char *start;
  RENode *last;
  RENode *n, *t, *t2;
  int tmp;
  int s, e;
  int i, j;

  recurse += 1;

  if (!end)
    end = pattern + strlen(pattern);

  last = NULL;
  n = NULL;

  while (pattern != end) {
    switch (*pattern) {
    case '(':
      if (last)
        n = (n ? re_cat_node(n, last) : last);

      pattern += 1;
      start = pattern;
      tmp = 1;

      while (pattern != end) {
        if (*pattern == '(') {
          tmp += 1;
        } else if (*pattern == ')') {
          tmp -= 1;
          if (tmp == 0)
            break;
        }
        pattern += 1;
      }

      if (*pattern != ')')
        goto done;

      t = re_parse(start, pattern, flags);
      if (t)
        last = re_paren_node(t);
      pattern += 1;
      break;

    case '[':
      if (last)
        n = (n ? re_cat_node(n, last) : last);

      pattern += 1;
      start = pattern;
      while ((pattern != end) && (*pattern != ']'))
        pattern += 1;

      if (*pattern != ']')
        goto done;

      if ((pattern - start) > 0)
        last = re_cclass_node(start, pattern, flags);
      else
        last = re_epsilon_node();
      pattern += 1;
      break;

    case '"':
      if (last)
        n = (n ? re_cat_node(n, last) : last);

      pattern += 1;
      t = NULL;

      while ((pattern != end) && (*pattern != '"')) {
        last = re_char_node(pattern, end, flags);
        t = (t ? re_cat_node(t, last) : last);
      }

      if (*pattern != '"') {
        goto done;
      }

      last = re_string_node(t);
      pattern += 1;
      break;

    case '#':
      if (last) {
        n = (n ? re_cat_node(n, last) : last);
      }

      pattern += 1;
      tmp = 0;

      while ((pattern != end) && (*pattern != '#')) {
        if (!isdigit(*pattern)) {
          goto done;
        }
        tmp *= 10;
        tmp += *pattern - '0';
        pattern += 1;
      }

      if (*pattern != '#') {
        goto done;
      }

      last = re_end_node(tmp);
      pattern += 1;
      break;

    case '|':
      if (last) {
        n = (n ? re_cat_node(n, last) : last);
      }

      pattern += 1;
      n = re_or_node(n, re_parse(pattern, end, flags));
      goto done;

    case '*':
      if (last && ((flags & RE_NO_WILDCARDS) == 0)) {
        last = re_star_node(last);
        pattern += 1;
      } else {
        if (last) {
          n = (n ? re_cat_node(n, last) : last);
        }
        last = re_char_node(pattern, end, flags);
      }
      break;

    case '+':
      if (last && ((flags & RE_NO_WILDCARDS) == 0)) {
        n = (n ? re_cat_node(n, last) : last);
        last = re_star_node(re_dup_node(last));
        pattern += 1;
      } else {
        if (last) {
          n = (n ? re_cat_node(n, last) : last);
        }
        last = re_char_node(pattern, end, flags);
      }
      break;

    case '?':
      if (last && ((flags & RE_NO_WILDCARDS) == 0)) {
        last = re_paren_node(re_or_node(re_epsilon_node(), last));
        pattern += 1;
      } else {
        if (last) {
          n = (n ? re_cat_node(n, last) : last);
        }
        last = re_char_node(pattern, end, flags);
      }
      break;

    case '{':
      if (last) {
        pattern += 1;
        start = pattern;

        tmp = 0;
        s = e = -1;

        while (pattern != end) {
          if (isdigit(*pattern)) {
            tmp *= 10;
            tmp += *pattern - '0';
            s = 1;
          } else if ((*pattern == ',') || (*pattern == '}')) {
            break;
          }
          pattern += 1;
        }

        if (s != -1) {
          s = tmp;
        }

        if (*pattern == ',') {
          pattern += 1;

          tmp = 0;
          while (pattern != end) {
            if (isdigit(*pattern)) {
              tmp *= 10;
              tmp += *pattern - '0';
              e = 1;
            } else if (*pattern == '}') {
              break;
            }
            pattern += 1;
          }

          if (e != -1) {
            e = tmp;
          }
        } else {
          e = s;
        }

        if (*pattern != '}') {
          goto done;
        }
        pattern += 1;

        if (s == -1) {
          last = re_star_node(last);
        } else if (e == -1) {
          for (i = 0; i < s; i++) {
            n = (n ? re_cat_node(n, last) : last);
            last = re_dup_node(last);
          }
          last = re_star_node(last);
        } else {
          t2 = NULL;

          for (j = s; j <= e; j++) {
            t = NULL;

            for (i = 0; i < j; i++) {
              t = (t ? re_cat_node(t, last) : last);
              if (((j + 1) <= e) || ((i + 1) < j)) {
                last = re_dup_node(last);
              }
            }

            t2 = (t2 ? re_or_node(t2, t) : t);
          }

          last = re_paren_node(t2);
        }
      } else {
        last = re_char_node(pattern, end, flags);
      }
      break;

    default:
      if (last) {
        n = (n ? re_cat_node(n, last) : last);
      }

      last = re_char_node(pattern, end, flags);
      break;
    }
  }

  if (last) {
    n = (n ? re_cat_node(n, last) : last);
  }

done:
  recurse -= 1;
  if (recurse == 0) {
    if (!last || (last->type != RE_NODE_END)) {
      last = re_end_node(default_end_value);
      n = (n ? re_cat_node(n, last) : last);
    }
    re_compute(n);
  }

  return n;
}

static unsigned char
re_parse_char(const char *&buf, const char *end)
{
  unsigned char c;
  int i;

  switch (*buf) {
  case '\\':
    buf += 1;

    if (buf == end) {
      c = '\\';
      break;
    }

    switch (*buf) {
    case 'a':
      c = '\a';
      break;
    case 'b':
      c = '\b';
      break;
    case 'f':
      c = '\f';
      break;
    case 'n':
      c = '\n';
      break;
    case 'r':
      c = '\r';
      break;
    case 't':
      c = '\t';
      break;
    case 'v':
      c = '\v';
      break;
    case '0':
    case '1':
    case '2':
    case '3':
      if (((end - buf) >= 3) && ((buf[1] >= '0') && (buf[1] <= '7')) && ((buf[2] >= '0') && (buf[2] <= '7'))) {
        i = (buf[0] - '0') * 64 + (buf[1] - '0') * 8 + buf[2];
        if (i < 256) {
          c = i;
          buf += 3;
        } else {
          c = (unsigned char) *buf++;
        }
      } else {
        c = (unsigned char) *buf++;
      }
      break;
    case 'x':
      if (((end - buf) >= 3) && isxdigit(buf[1]) && isxdigit(buf[2])) {
        if (isalpha(buf[1])) {
          c = (toupper(buf[1]) - 'A' + 10) * 16;
        } else {
          c = (buf[1] - '0') * 16;
        }
        if (isalpha(buf[2])) {
          c += (toupper(buf[2]) - 'A' + 10);
        } else {
          c += buf[2] - '0';
        }
        buf += 3;
      } else
        c = (unsigned char) *buf++;
      break;
    default:
      c = (unsigned char) *buf++;
      break;
    }
    break;

  default:
    c = (unsigned char) *buf++;
    break;
  }

  return c;
}

static RENode *
node_alloc(RENodeType type)
{
  RENode *n;

  if (node_free_list) {
    n = node_free_list;
    node_free_list = node_free_list->next;
  } else {
    n = NEW(new RENode);
  }

  n->next = node_alloc_list;
  node_alloc_list = n;

  n->type = type;
  n->id = next_node_id++;
  n->follow_nodes = NULL;
  n->first_nodes = NULL;
  n->last_nodes = NULL;

  return n;
}

static RENode **
nodes_alloc(int n)
{
  RENodes *tmp_nodes;
  int tmp;

  if (!nodes_free_list || ((nodes_pos + n) > nodes_free_list->size)) {
    nodes_pos = 0;

    if (nodes_free_list) {
      do {
        tmp_nodes = nodes_free_list;
        nodes_free_list = nodes_free_list->next;
        tmp_nodes->next = nodes_alloc_list;
        nodes_alloc_list = tmp_nodes;
      } while (nodes_free_list && (n > nodes_free_list->size));
    }

    if (!nodes_free_list) {
      tmp = ((n > 1024) ? n : 1024);
      tmp_nodes = (RENodes *)
      NEW(new char[sizeof(RENodes) + sizeof(RENode *) * (tmp - 1024)]);
      tmp_nodes->next = NULL;
      tmp_nodes->size = tmp;
      nodes_free_list = tmp_nodes;
    }
  }

  tmp = nodes_pos;
  nodes_pos += n;

  return &nodes_free_list->nodes[tmp];
}

static RENode *
re_end_node(int accept_num)
{
  RENode *n;

  n = node_alloc(RE_NODE_END);
  n->u.accept_num = accept_num;

  return n;
}

static RENode *
re_epsilon_node()
{
  return node_alloc(RE_NODE_EPSILON);
}

static RENode *
re_char_node(const char *&buf, const char *end, REFlags flags)
{
  RENode *n;
  unsigned char c;
  int i;

  n = node_alloc(RE_NODE_CCLASS);

  if (*buf == '.') {
    buf += 1;
    for (i = 0; i < 32; i++) {
      n->u.cclass[i] = 0xff;
    }
    n->u.cclass[32] = 0;
  } else {
    for (i = 0; i < 33; i++) {
      n->u.cclass[i] = 0;
    }

    c = re_parse_char(buf, end);

    if (flags & RE_CASE_INSENSITIVE) {
      bitops_set(n->u.cclass, tolower(c));
      bitops_set(n->u.cclass, toupper(c));
    } else {
      bitops_set(n->u.cclass, c);
    }
  }

  return n;
}

static RENode *
re_cclass_node(const char *&buf, const char *end, REFlags flags)
{
  RENode *n;
  bool negate;
  int prev, next;
  int i, s, e;

  n = node_alloc(RE_NODE_CCLASS);

  for (i = 0; i < 33; i++)
    n->u.cclass[i] = 0;

  negate = false;
  if (*buf == '^') {
    negate = true;
    buf += 1;
  }

  prev = 0;
  while (buf != end) {
    switch (*buf) {
    case '-':
      if (((buf + 1) != end) || !prev) {
        buf += 1;

        next = re_parse_char(buf, end);

        if ((islower(prev) && isupper(next)) || (isupper(prev) && islower(next))) {
          s = toupper(prev);
          e = 'Z';

          if (flags & RE_CASE_INSENSITIVE) {
            for (i = s; i <= e; i++) {
              bitops_set(n->u.cclass, tolower(i));
              bitops_set(n->u.cclass, toupper(i));
            }
          } else {
            for (i = s; i <= e; i++) {
              bitops_set(n->u.cclass, i);
            }
          }

          s = 'a';
          e = tolower(next);

          if (flags & RE_CASE_INSENSITIVE) {
            for (i = s; i <= e; i++) {
              bitops_set(n->u.cclass, tolower(i));
              bitops_set(n->u.cclass, toupper(i));
            }
          } else {
            for (i = s; i <= e; i++) {
              bitops_set(n->u.cclass, i);
            }
          }
        } else {
          if (next > prev) {
            s = prev;
            e = next;
          } else {
            s = next;
            e = prev;
          }

          if (flags & RE_CASE_INSENSITIVE) {
            for (i = s; i <= e; i++) {
              bitops_set(n->u.cclass, tolower(i));
              bitops_set(n->u.cclass, toupper(i));
            }
          } else {
            for (i = s; i <= e; i++) {
              bitops_set(n->u.cclass, i);
            }
          }
        }

        prev = next;
        break;
      }

    default:
      prev = re_parse_char(buf, end);

      if (flags & RE_CASE_INSENSITIVE) {
        bitops_set(n->u.cclass, tolower(prev));
        bitops_set(n->u.cclass, toupper(prev));
      } else {
        bitops_set(n->u.cclass, prev);
      }
      break;
    }
  }

  if (negate) {
    for (i = 0; i < 32; i++) {
      n->u.cclass[i] = ~n->u.cclass[i];
    }
  }

  return n;
}

static RENode *
re_cat_node(RENode * left, RENode * right)
{
  RENode *n;

  n = node_alloc(RE_NODE_CAT);
  n->u.cat.left = left;
  n->u.cat.right = right;

  return n;
}

static RENode *
re_or_node(RENode * left, RENode * right)
{
  RENode *n;

  n = node_alloc(RE_NODE_OR);
  n->u._or.left = left;
  n->u._or.right = right;

  return n;
}

static RENode *
re_star_node(RENode * child)
{
  RENode *n;

  n = node_alloc(RE_NODE_STAR);
  n->u.star.child = child;

  return n;
}

static RENode *
re_paren_node(RENode * child)
{
  RENode *n;

  n = node_alloc(RE_NODE_PAREN);
  n->u.paren.child = child;

  return n;
}

static RENode *
re_string_node(RENode * child)
{
  RENode *n;

  n = node_alloc(RE_NODE_STRING);
  n->u.string.child = child;

  return n;
}

static RENode *
re_dup_node(RENode * n)
{
  RENode *new_node;
  int i;

  new_node = node_alloc(n->type);

  switch (n->type) {
  case RE_NODE_END:
  case RE_NODE_EPSILON:
    break;
  case RE_NODE_CCLASS:
    for (i = 0; i < 33; i++) {
      new_node->u.cclass[i] = n->u.cclass[i];
    }
    break;
  case RE_NODE_CAT:
    new_node->u.cat.left = re_dup_node(n->u.cat.left);
    new_node->u.cat.right = re_dup_node(n->u.cat.right);
    break;
  case RE_NODE_OR:
    new_node->u._or.left = re_dup_node(n->u._or.left);
    new_node->u._or.right = re_dup_node(n->u._or.right);
    break;
  case RE_NODE_STAR:
    new_node->u.star.child = re_dup_node(n->u.star.child);
    break;
  case RE_NODE_PAREN:
    new_node->u.paren.child = re_dup_node(n->u.paren.child);
    break;
  case RE_NODE_STRING:
    new_node->u.string.child = re_dup_node(n->u.string.child);
    break;
  }

  return new_node;
}

static void
re_compute(RENode * n)
{
  RENode **nodes1;
  RENode **nodes2;
  int i;

  switch (n->type) {
  case RE_NODE_END:
    break;
  case RE_NODE_EPSILON:
    break;
  case RE_NODE_CCLASS:
    break;
  case RE_NODE_CAT:
    re_compute(n->u.cat.left);
    re_compute(n->u.cat.right);

    nodes1 = re_last_nodes(n->u.cat.left);
    nodes2 = re_first_nodes(n->u.cat.right);

    for (i = 0; nodes1[i]; i++) {
      nodes1[i]->follow_nodes = re_union_nodes(nodes1[i]->follow_nodes, nodes2);
    }
    break;
  case RE_NODE_OR:
    re_compute(n->u.cat.left);
    re_compute(n->u.cat.right);
    break;
  case RE_NODE_STAR:
    re_compute(n->u.star.child);

    nodes1 = re_last_nodes(n);
    nodes2 = re_first_nodes(n);

    for (i = 0; nodes1[i]; i++) {
      nodes1[i]->follow_nodes = re_union_nodes(nodes1[i]->follow_nodes, nodes2);
    }
    break;
  case RE_NODE_PAREN:
    re_compute(n->u.paren.child);
    break;
  case RE_NODE_STRING:
    re_compute(n->u.string.child);
    break;
  }
}

static bool
re_nullable(RENode * n)
{
  switch (n->type) {
  case RE_NODE_END:
    return false;
  case RE_NODE_EPSILON:
    return true;
  case RE_NODE_CCLASS:
    return false;
  case RE_NODE_CAT:
    return (bool) (re_nullable(n->u.cat.left) && re_nullable(n->u.cat.right));
  case RE_NODE_OR:
    return (bool) (re_nullable(n->u.cat.left) || re_nullable(n->u.cat.right));
  case RE_NODE_STAR:
    return true;
  case RE_NODE_PAREN:
    return re_nullable(n->u.paren.child);
  case RE_NODE_STRING:
    return re_nullable(n->u.string.child);
  }

  return true;
}

static RENode **
re_first_nodes(RENode * n)
{
  switch (n->type) {
  case RE_NODE_END:
    if (!n->first_nodes) {
      n->first_nodes = nodes_alloc(2);
      n->first_nodes[0] = n;
      n->first_nodes[1] = NULL;
    }
    break;
  case RE_NODE_EPSILON:
    if (!n->first_nodes) {
      n->first_nodes = nodes_alloc(1);
      n->first_nodes[0] = NULL;
    }
    break;
  case RE_NODE_CCLASS:
    if (!n->first_nodes) {
      n->first_nodes = nodes_alloc(2);
      n->first_nodes[0] = n;
      n->first_nodes[1] = NULL;
    }
    break;
  case RE_NODE_CAT:
    if (!n->first_nodes) {
      if (re_nullable(n->u.cat.left)) {
        n->first_nodes = re_union_nodes(re_first_nodes(n->u.cat.left), re_first_nodes(n->u.cat.right));
      } else {
        n->first_nodes = re_first_nodes(n->u.cat.left);
      }
    }
    break;
  case RE_NODE_OR:
    if (!n->first_nodes) {
      n->first_nodes = re_union_nodes(re_first_nodes(n->u.cat.left), re_first_nodes(n->u.cat.right));
    }
    break;
  case RE_NODE_STAR:
    if (!n->first_nodes) {
      n->first_nodes = re_first_nodes(n->u.star.child);
    }
    break;
  case RE_NODE_PAREN:
    if (!n->first_nodes) {
      n->first_nodes = re_first_nodes(n->u.paren.child);
    }
    break;
  case RE_NODE_STRING:
    if (!n->first_nodes) {
      n->first_nodes = re_first_nodes(n->u.string.child);
    }
    break;
  }

  return n->first_nodes;
}

static RENode **
re_last_nodes(RENode * n)
{
  switch (n->type) {
  case RE_NODE_END:
    if (!n->last_nodes) {
      n->last_nodes = nodes_alloc(2);
      n->last_nodes[0] = n;
      n->last_nodes[1] = NULL;
    }
    break;
  case RE_NODE_EPSILON:
    if (!n->last_nodes) {
      n->last_nodes = nodes_alloc(1);
      n->last_nodes[0] = NULL;
    }
    break;
  case RE_NODE_CCLASS:
    if (!n->last_nodes) {
      n->last_nodes = nodes_alloc(2);
      n->last_nodes[0] = n;
      n->last_nodes[1] = NULL;
    }
    break;
  case RE_NODE_CAT:
    if (!n->last_nodes) {
      if (re_nullable(n->u.cat.right)) {
        n->last_nodes = re_union_nodes(re_last_nodes(n->u.cat.left), re_last_nodes(n->u.cat.right));
      } else {
        n->last_nodes = re_last_nodes(n->u.cat.right);
      }
    }
    break;
  case RE_NODE_OR:
    if (!n->last_nodes) {
      n->last_nodes = re_union_nodes(re_last_nodes(n->u.cat.left), re_last_nodes(n->u.cat.right));
    }
    break;
  case RE_NODE_STAR:
    if (!n->last_nodes) {
      n->last_nodes = re_last_nodes(n->u.star.child);
    }
    break;
  case RE_NODE_PAREN:
    if (!n->last_nodes) {
      n->last_nodes = re_last_nodes(n->u.paren.child);
    }
    break;
  case RE_NODE_STRING:
    if (!n->first_nodes) {
      n->last_nodes = re_last_nodes(n->u.string.child);
    }
    break;
  }

  return n->last_nodes;
}

static RENode **
re_union_nodes(RENode ** a, RENode ** b)
{
  RENode **new_nodes;
  int ca, cb;
  int i;

  if (!a) {
    return b;
  }
  if (!b) {
    return a;
  }

  for (ca = 0; a[ca]; ca++);
  for (cb = 0; b[cb]; cb++);

  new_nodes = nodes_alloc(ca + cb + 1);

  for (i = 0; a[0] || b[0]; i++) {
    if (a[0] == b[0]) {
      new_nodes[i] = *a++;
      b++;
    } else if (a[0] && b[0]) {
      if (a[0]->id < b[0]->id) {
        new_nodes[i] = *a++;
      } else {
        new_nodes[i] = *b++;
      }
    } else if (a[0]) {
      new_nodes[i] = *a++;
    } else {
      new_nodes[i] = *b++;
    }
  }

  new_nodes[i] = NULL;

  return new_nodes;
}

static bool
re_equal_nodes(RENode ** a, RENode ** b)
{
  while (a[0] && b[0]) {
    if (a[0] != b[0]) {
      return false;
    }
    a += 1;
    b += 1;
  }

  if (a[0] != b[0]) {
    return false;
  }
  return true;
}

static void
re_print_node(RENode * n)
{
  static int recurse = 0;

  recurse += 1;

  switch (n->type) {
  case RE_NODE_END:
    break;
  case RE_NODE_EPSILON:
    printf("[]");
    break;
  case RE_NODE_CCLASS:
    re_print_cclass(n->u.cclass);
    break;
  case RE_NODE_CAT:
    re_print_node(n->u.cat.left);
    re_print_node(n->u.cat.right);
    break;
  case RE_NODE_OR:
    re_print_node(n->u._or.left);
    printf("|");
    re_print_node(n->u._or.right);
    break;
  case RE_NODE_STAR:
    re_print_node(n->u.star.child);
    printf("*");
    break;
  case RE_NODE_PAREN:
    printf("(");
    re_print_node(n->u.paren.child);
    printf(")");
    break;
  case RE_NODE_STRING:
    printf("\"");
    re_print_node(n->u.string.child);
    printf("\"");
    break;
  }

  recurse -= 1;
  if (recurse == 0) {
    printf("\n");
  }
}


static void
re_print_state(REState * state)
{
  RETransition *t;

  if (state->marked) {
    return;
  }
  state->marked = true;

  printf("%3d: ", (int)state->id);
  if (state->accept_num != -1) {
    printf("accept (%d)", state->accept_num);
  }
  printf("\n");

  t = state->transitions;
  while (t) {
    printf("     ");
    re_print_cclass(t->cclass);
    printf(" --> %d\n", (int)t->state->id);
    t = t->next;
  }

  t = state->transitions;
  while (t) {
    re_print_state(t->state);
    t = t->next;
  }
}

static void
re_print_char(unsigned char c)
{
  switch (c) {
  case '-':
  case '.':
  case '*':
  case '?':
  case '+':
  case '"':
  case '(':
  case ')':
  case '[':
  case ']':
  case '{':
  case '}':
  case '|':
  case '#':
    printf("\\%c", c);
    break;
  case ' ':
    printf("' '");
    break;
  default:
    if (isprint(c)) {
      printf("%c", c);
    } else {
      printf("\\x%02x", c);
    }
    break;
  }
}

static void
re_print_cclass(unsigned char *cclass)
{
  int prev;
  int next;

  prev = bitops_next_set(cclass, cclass + 32, -1);
  if (prev >= 0) {
    next = bitops_next_unset(cclass, cclass + 33, prev) - 1;

    if (prev == next) {
      re_print_char(prev);
    } else if (next == 255) {
      printf(".");
    } else {
      printf("[");

      re_print_char(prev);
      printf("-");
      re_print_char(next);

      while ((prev >= 0) && (next >= 0)) {
        prev = bitops_next_set(cclass, cclass + 32, next);
        if (prev >= 0) {
          re_print_char(prev);
          next = bitops_next_unset(cclass, cclass + 33, prev) - 1;

          if (prev != next) {
            printf("-");
            re_print_char(next);
          }
        } else {
          next = 0;
        }
      }

      printf("]");
    }
  } else {
    printf("[]");
  }
}

static void
re_print_nodes(RENode ** nodes)
{
  int i;

  printf("{");

  if (nodes) {
    for (i = 0; nodes[i]; i++) {
      printf(" %d%s", nodes[i]->id, (nodes[i + 1] ? "," : ""));
    }
  }

  printf(" }\n");
}

static void
re_debug(RENode * n)
{
  printf("%2d: ", n->id);
  re_print_nodes(n->follow_nodes);

  switch (n->type) {
  case RE_NODE_END:
    break;
  case RE_NODE_EPSILON:
    break;
  case RE_NODE_CCLASS:
    break;
  case RE_NODE_CAT:
    re_debug(n->u.cat.left);
    re_debug(n->u.cat.right);
    break;
  case RE_NODE_OR:
    re_debug(n->u._or.left);
    re_debug(n->u._or.right);
    break;
  case RE_NODE_STAR:
    re_debug(n->u.star.child);
    break;
  case RE_NODE_PAREN:
    re_debug(n->u.paren.child);
    break;
  case RE_NODE_STRING:
    re_debug(n->u.string.child);
    break;
  }
}


static REState *default_state = NULL;

static REState **
re_construct(RENode * n, int *count)
{
  DynArray<REState *>dstates(&default_state);
  REState **states;
  REState *state;
  REState *new_state;
  RETransition *t;
  unsigned char *cclass;
  RENode **nodes;
  RENode **new_nodes;
  intptr_t cur_state;
  int nstates;
  int input;
  int i;
  intptr_t j;

  nstates = 1;
  cur_state = 0;
  dstates(0) = state_alloc(re_first_nodes(n));

  for (;;) {
    state = NULL;
    for (; cur_state < nstates; cur_state++) {
      if (!dstates[cur_state]->marked) {
        state = dstates[cur_state];
        break;
      }
    }

    if (!state) {
      break;
    }
    state->marked = true;

    nodes = state->nodes;
    for (i = 0; nodes[i]; i++) {
      if (nodes[i]->type == RE_NODE_CCLASS) {
        cclass = nodes[i]->u.cclass;
        input = bitops_next_set(cclass, cclass + 32, -1);

        while (input != -1) {
          new_nodes = nodes[i]->follow_nodes;

          for (j = 0; nodes[j]; j++) {
            if ((i != j) &&
                (nodes[j]->type == RE_NODE_CCLASS) &&
                bitops_isset(nodes[j]->u.cclass, input) && nodes[j]->follow_nodes) {
              new_nodes = re_union_nodes(new_nodes, nodes[j]->follow_nodes);
            }
          }

          new_state = NULL;
          for (j = 0; j < nstates; j++) {
            if (re_equal_nodes(new_nodes, dstates[j]->nodes)) {
              new_state = dstates[j];
              break;
            }
          }

          if (new_state) {
            t = state->transitions;
            while (t) {
              if (t->state == new_state) {
                bitops_set(t->cclass, input);
                bitops_set(state->cclass, input);
                break;
              }
              t = t->next;
            }

            if (!t) {
              transition_add(state, transition_alloc(input, new_state));
            }
          } else {
            new_state = state_alloc(new_nodes);
            dstates(nstates++) = new_state;
            transition_add(state, transition_alloc(input, new_state));
          }

          input = bitops_next_set(cclass, cclass + 32, input);
        }
      }
    }
  }

  states = dstates.detach();
  *count = dstates.length();

  return states;
}

static REState *
state_alloc(RENode ** nodes)
{
  REState *s;
  int i;

  if (state_free_list) {
    s = state_free_list;
    state_free_list = state_free_list->next;
  } else {
    s = NEW(new REState);
  }

  s->next = state_alloc_list;
  state_alloc_list = s;

  s->id = next_state_id++;
  s->accept_num = -1;
  s->marked = false;
  s->transitions = NULL;
  s->nodes = nodes;

  for (i = 0; nodes[i]; i++) {
    if (nodes[i]->type == RE_NODE_END) {
      s->accept_num = nodes[i]->u.accept_num;
      break;
    }
  }

  for (i = 0; i < 33; i++) {
    s->cclass[i] = 0;
  }

  return s;
}

static RETransition *
transition_alloc(int input, REState * state)
{
  RETransition *t;
  int i;

  if (transition_free_list) {
    t = transition_free_list;
    transition_free_list = transition_free_list->next_free;
  } else {
    t = NEW(new RETransition);
  }

  t->next_free = transition_alloc_list;
  transition_alloc_list = t;

  t->state = state;
  t->next = NULL;

  for (i = 0; i < 33; i++) {
    t->cclass[i] = 0;
  }

  bitops_set(t->cclass, input);

  return t;
}

static void
transition_add(REState * state, RETransition * t)
{
  t->next = state->transitions;
  state->transitions = t;
  bitops_union(state->cclass, t->cclass, 32);
}

static void
state_clear(REState * state)
{
  RETransition *t;

  if (!state->marked) {
    return;
  }
  state->marked = false;

  t = state->transitions;
  while (t) {
    state_clear(t->state);
    t = t->next;
  }
}


static void
re_build(REState ** states,
         int nstates, DynArray<int>&base, DynArray<int>&accept, DynArray<int>&next, DynArray<int>&check)
{
  REState *s;
  RETransition *t;
  int *counts;
  int input;
  intptr_t i;
  int j;
  intptr_t b;
  int c;

  /* allocate the counts array */
  counts = NEW(new int[nstates]);

  /* determine the input transition counts for each state */
  for (i = 0; i < nstates; i++) {
    counts[i] = bitops_count(states[i]->cclass, states[i]->cclass + 32);
  }

  /* sort the states based on their input transition counts */
  for (i = 1; i < nstates; i++) {
    j = i;
    s = states[j];
    c = counts[j];

    while ((j > 0) && (counts[j - 1] < c)) {
      states[j] = states[j - 1];
      counts[j] = counts[j - 1];
      j -= 1;
    }

    states[j] = s;
    counts[j] = c;
  }

  /* preempt the dynamic array code to expand the array
     since we know how big the accept table is supposed
     to be. */
  accept(nstates) = -1;

  /* initialize the accept table */
  for (i = 0; i < nstates; i++) {
    accept[states[i]->id] = states[i]->accept_num;
  }

  /* build the compressed state transition tables */
  for (i = 0; i < nstates; i++) {
    s = states[i];

    /* we need to find a base for this state. this is accomplished
       by looping over the possible bases (starting at 0) and selecting
       the first base for which all inputs for this state have an
       empty location in the "check" table. */
    for (b = 0, input = 0;; b++) {
      /* we loop over the inputs checking the "check" table to
         make sure it is clear for these inputs. */
      input = bitops_next_set(s->cclass, s->cclass + 32, -1);
      while (input != -1) {
        if (check(b + input) != -1) {
          break;
        }
        input = bitops_next_set(s->cclass, s->cclass + 32, input);
      }
      if (input == -1) {
        break;
      }
    }

    /* when we get here we have selected a valid base, so we set
       the base location for this state to be the base we selected. */
    base(s->id) = b;

    /* now we have to loop over the transitions out of this state and
       set the "check" and "next" tables appropriately. we know at this
       point that the "check" table will contain -1 for each location
       we are trying to set and the "next" table entry will contain 0. */

    /* preempt the dynamic array code to expand the array allowing
       us to call the quicker array operator below. */
    if (check(b + 255) == -1) {
      next(b + 255) = 0;
    }

    t = s->transitions;
    while (t) {
      input = bitops_next_set(t->cclass, t->cclass + 32, -1);
      while (input != -1) {
        /* assert (check[b + input] == -1); */
        check[b + input] = s->id;
        next[b + input] = t->state->id;
        input = bitops_next_set(t->cclass, t->cclass + 32, input);
      }

      t = t->next;
    }
  }

  /* cleanup the temporary arrays */
  delete[]states;
  delete[]counts;
}

static const ink32 negative_one = -1;
static const ink32 zero = 0;

DFA::DFA()
:basetbl(&negative_one), accepttbl(&negative_one), nexttbl(&zero), checktbl(&negative_one)
{
}

DFA::~DFA()
{
}

int
DFA::compile(const char *pattern, REFlags flags)
{
  RENode *n;
  REState **s;
  int nstates;

  n = re_parse(pattern, NULL, flags);
  // re_print_node (n);

  s = re_construct(n, &nstates);
  state_clear(s[0]);
  // re_print_state (s[0]);
  // printf ("Number of uncompressed states: %d\n", nstates);

  re_build(s, nstates, basetbl, accepttbl, nexttbl, checktbl);

  re_free();

  return 0;
}

int
DFA::compile(const char **patterns, int npatterns, REFlags flags)
{
  RENode *n1, *n2;
  REState **s;
  const char *pattern;
  int nstates;
  int i;

  n1 = NULL;
  for (i = 0; i < npatterns; i++) {
    pattern = patterns[i];
    n2 = re_parse(pattern, NULL, flags, i);
    n1 = (n1 ? re_or_node(n1, n2) : n2);
  }

  // re_print_node (n1);

  s = re_construct(n1, &nstates);

  // re_print_state (s[0]);
  // printf ("Number of uncompressed states: %d\n", nstates);

  re_build(s, nstates, basetbl, accepttbl, nexttbl, checktbl);

  re_free();

  return 0;
}

int
DFA::compile(const char *filename, const char **patterns, int npatterns, REFlags flags)
{
  static ink32 magic = 0x01020304;
  DynArray<int>*tables[4];
  int length;
  INK_DIGEST_CTX md5_context;
  INK_MD5 md5;
  int err;
  int fd;
  int i;

  ink_assert(filename != NULL);

  ink_code_incr_md5_init(&md5_context);
  ink_code_incr_md5_update(&md5_context, (char *) &npatterns, sizeof(npatterns));
  ink_code_incr_md5_update(&md5_context, (char *) &flags, sizeof(flags));

  for (i = 0; i < npatterns; i++) {
    ink_code_incr_md5_update(&md5_context, patterns[i], (int) strlen(patterns[i]));
  }

  ink_code_incr_md5_final((char *) &md5, &md5_context);

#ifndef TS_MICRO
  fd = ink_open(filename, O_RDONLY | _O_ATTRIB_NORMAL);
#else
  fd = -1;
#endif
  if (fd > 0) {
    INK_MD5 old_md5;
    ink32 old_magic;

    tables[0] = &basetbl;
    tables[1] = &accepttbl;
    tables[2] = &nexttbl;
    tables[3] = &checktbl;

    err = ink_read(fd, &old_magic, sizeof(old_magic));
    if ((err != sizeof(old_magic)) || (old_magic != magic)) {
      goto fail;
    }

    err = ink_read(fd, &old_md5, sizeof(old_md5));
    if ((err != sizeof(old_md5)) || (!(old_md5 == md5))) {
      goto fail;
    }

    for (i = 0; i < 4; i++) {
      err = ink_read(fd, &length, sizeof(length));
      if (err != sizeof(length)) {
        goto fail;
      }
      tables[i]->set_length(length);
      (*tables[i]) (tables[i]->length() - 1) = tables[i]->defvalue();

      err = ink_read(fd, (ink32 *) (*tables[i]), sizeof(ink32) * (tables[i]->length()));
      if (err != (int) (sizeof(ink32) * (tables[i]->length()))) {
        goto fail;
      }
    }

    ink_close(fd);
    return 0;

  fail:
    tables[0]->clear();
    tables[1]->clear();
    tables[2]->clear();
    tables[3]->clear();
    ink_close(fd);
  }

  err = compile(patterns, npatterns, flags);
  if (err < 0) {
    return err;
  }
#ifndef TS_MICRO
  fd = ink_open(filename, O_CREAT | O_TRUNC | O_WRONLY | _O_ATTRIB_NORMAL, 0755);
  if (fd < 0) {
    return 0;
  }

  err = ink_write(fd, &magic, sizeof(magic));
  if (err != sizeof(magic)) {
    goto done;
  }

  err = ink_write(fd, &md5, sizeof(md5));
  if (err != sizeof(md5)) {
    goto done;
  }

  tables[0] = &basetbl;
  tables[1] = &accepttbl;
  tables[2] = &nexttbl;
  tables[3] = &checktbl;

  for (i = 0; i < 4; i++) {
    length = tables[i]->length();
    err = ink_write(fd, &length, sizeof(length));
    if (err != sizeof(length)) {
      goto done;
    }

    err = ink_write(fd, (ink32 *) (*tables[i]), sizeof(ink32) * (tables[i]->length()));
    if (err != (int) (sizeof(ink32) * (tables[i]->length()))) {
      goto done;
    }
  }

done:
  ink_close(fd);
#endif // TS_MICRO
  return 0;
}

int
DFA::size()
{
  return nexttbl.length();
}


int
DFA::match(const char *str)
{
  const int *base;
  const int *check;
  const int *next;
  const int *accept;
  int s, t;

  s = 0;

  base = basetbl;
  check = checktbl;
  next = nexttbl;
  accept = accepttbl;

  while (*str) {
    t = base[s] + (unsigned char) *str++;
    if (check[t] != s) {
      return -1;
    }
    s = next[t];
  }

  return accept[s];
}

int
DFA::match(const char *str, int length)
{
  const char *end;
  const int *base;
  const int *check;
  const int *next;
  const int *accept;
  int s, t;

//    printf("DFA::match(%.*s)\n",length,str);

  s = 0;
  end = str + length;

  base = basetbl;
  check = checktbl;
  next = nexttbl;

  while (str != end) {
    t = base[s] + (unsigned char) *str++;
    if (check[t] != s) {
      return -2;
    }
    s = next[t];
  }

  accept = accepttbl;
  return accept[s];
}

int
DFA::match(const char *&str, int length, int &state)
{
  const char *end;
  const char *start;
  const int *base;
  const int *check;
  const int *next;
  const int *accept;
  int t;

  start = str;
  end = str + length;

  base = basetbl;
  check = checktbl;
  next = nexttbl;
  accept = accepttbl;

  while (str != end) {
    if (accept[state] != -1) {
      return accept[state];
    }

    t = base[state] + (unsigned char) *str++;
    if (check[t] != state) {
      str = start;
      state = 0;
      return -2;
    }
    state = next[t];
  }

  return accept[state];
}
