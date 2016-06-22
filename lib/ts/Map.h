/** @file

  A set of Map templates.

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

#ifndef _map_H_
#define _map_H_

#include <stdlib.h>
#include <string.h>

#include "ts/ink_assert.h"
#include "ts/List.h"
#include "ts/Vec.h"

#define MAP_INTEGRAL_SIZE (1 << (2))
//#define MAP_INITIAL_SHIFT               ((2)+1)
//#define MAP_INITIAL_SIZE                (1 << MAP_INITIAL_SHIFT)

typedef const char cchar;

template <class A>
static inline char *
_dupstr(cchar *s, cchar *e = 0)
{
  int l    = e ? e - s : strlen(s);
  char *ss = (char *)A::alloc(l + 1);
  memcpy(ss, s, l);
  ss[l] = 0;
  return ss;
}

// Simple direct mapped Map (pointer hash table) and Environment

template <class K, class C> class MapElem
{
public:
  K key;
  C value;
  bool
  operator==(MapElem &e)
  {
    return e.key == key;
  }
  operator uintptr_t(void) { return (uintptr_t)(uintptr_t)key; }
  MapElem(uintptr_t x)
  {
    ink_assert(!x);
    key = 0;
  }
  MapElem(K akey, C avalue) : key(akey), value(avalue) {}
  MapElem(MapElem &e) : key(e.key), value(e.value) {}
  MapElem() : key(0) {}
};

template <class K, class C, class A = DefaultAlloc> class Map : public Vec<MapElem<K, C>, A>
{
public:
  typedef MapElem<K, C> ME;
  typedef Vec<ME, A> PType;
  using PType::n;
  using PType::i;
  using PType::v;
  ME *put(K akey, C avalue);
  ME *put(K akey);
  C get(K akey);
  C *getp(K akey);
  void get_keys(Vec<K> &keys);
  void get_keys_set(Vec<K> &keys);
  void get_values(Vec<C> &values);
  void map_union(Map<K, C> &m);
  bool some_disjunction(Map<K, C> &m) const;
};

template <class C> class HashFns
{
public:
  static uintptr_t hash(C a);
  static int equal(C a, C b);
};

template <class K, class C> class HashSetFns
{
public:
  static uintptr_t hash(C a);
  static uintptr_t hash(K a);
  static int equal(C a, C b);
  static int equal(K a, C b);
};

template <class K, class AHashFns, class C, class A = DefaultAlloc> class HashMap : public Map<K, C, A>
{
public:
  typedef MapElem<K, C> value_type; ///< What's stored in the table.
  using Map<K, C, A>::n;
  using Map<K, C, A>::i;
  using Map<K, C, A>::v;
  using Map<K, C, A>::e;
  MapElem<K, C> *get_internal(K akey);
  C get(K akey);
  value_type *put(K akey, C avalue);
  void get_keys(Vec<K> &keys);
  void get_values(Vec<C> &values);
};

#define form_Map(_c, _p, _v)                                                                                                       \
  if ((_v).n)                                                                                                                      \
    for (_c *qq__##_p = (_c *)0, *_p = &(_v).v[0]; ((uintptr_t)(qq__##_p) < (_v).n) && ((_p = &(_v).v[(uintptr_t)qq__##_p]) || 1); \
         qq__##_p = (_c *)(((uintptr_t)qq__##_p) + 1))                                                                             \
      if ((_p)->key)

template <class K, class AHashFns, class C, class A = DefaultAlloc> class HashSet : public Vec<C, A>
{
public:
  typedef Vec<C, A> V;
  using V::n;
  using V::i;
  using V::v;
  using V::e;
  C get(K akey);
  C *put(C avalue);
};

class StringHashFns
{
public:
  static uintptr_t
  hash(cchar *s)
  {
    uintptr_t h = 0;
    // 31 changed to 27, to avoid prime2 in vec.cpp
    while (*s)
      h = h * 27 + (unsigned char)*s++;
    return h;
  }
  static int
  equal(cchar *a, cchar *b)
  {
    return !strcmp(a, b);
  }
};

class CaseStringHashFns
{
public:
  static uintptr_t
  hash(cchar *s)
  {
    uintptr_t h = 0;
    // 31 changed to 27, to avoid prime2 in vec.cpp
    while (*s)
      h = h * 27 + (unsigned char)toupper(*s++);
    return h;
  }
  static int
  equal(cchar *a, cchar *b)
  {
    return !strcasecmp(a, b);
  }
};

class PointerHashFns
{
public:
  static uintptr_t
  hash(void *s)
  {
    return (uintptr_t)(uintptr_t)s;
  }
  static int
  equal(void *a, void *b)
  {
    return a == b;
  }
};

template <class C, class AHashFns, class A = DefaultAlloc> class ChainHash : public Map<uintptr_t, List<C, A>, A>
{
public:
  using Map<uintptr_t, List<C, A>, A>::n;
  using Map<uintptr_t, List<C, A>, A>::v;
  typedef ConsCell<C, A> ChainCons;
  C put(C c);
  C get(C c);
  C put_bag(C c);
  int get_bag(C c, Vec<C> &v);
  int del(C avalue);
  void get_elements(Vec<C> &elements);
};

template <class K, class AHashFns, class C, class A = DefaultAlloc>
class ChainHashMap : public Map<uintptr_t, List<MapElem<K, C>, A>, A>
{
public:
  using Map<uintptr_t, List<MapElem<K, C>, A>, A>::n;
  using Map<uintptr_t, List<MapElem<K, C>, A>, A>::v;
  MapElem<K, C> *put(K akey, C avalue);
  C get(K akey);
  int del(K akey);
  MapElem<K, C> *put_bag(K akey, C c);
  int get_bag(K akey, Vec<C> &v);
  void get_keys(Vec<K> &keys);
  void get_values(Vec<C> &values);
};

template <class F = StringHashFns, class A = DefaultAlloc> class StringChainHash : public ChainHash<cchar *, F, A>
{
public:
  cchar *canonicalize(cchar *s, cchar *e);
  cchar *
  canonicalize(cchar *s)
  {
    return canonicalize(s, s + strlen(s));
  }
};

template <class C, class AHashFns, int N, class A = DefaultAlloc> class NBlockHash
{
public:
  int n;
  int i;
  C *v;
  C e[N];

  C *
  end()
  {
    return last();
  }
  int
  length()
  {
    return N * n;
  }
  C *first();
  C *last();
  C put(C c);
  C get(C c);
  C *assoc_put(C *c);
  C *assoc_get(C *c);
  int del(C c);
  void clear();
  void reset();
  int count();
  void size(int p2);
  void copy(const NBlockHash<C, AHashFns, N, A> &hh);
  void move(NBlockHash<C, AHashFns, N, A> &hh);
  NBlockHash();
  NBlockHash(NBlockHash<C, AHashFns, N, A> &hh)
  {
    v = e;
    copy(hh);
  }
};

/* use forv_Vec on BlockHashes */

#define DEFAULT_BLOCK_HASH_SIZE 4
template <class C, class ABlockHashFns> class BlockHash : public NBlockHash<C, ABlockHashFns, DEFAULT_BLOCK_HASH_SIZE>
{
};
typedef BlockHash<cchar *, StringHashFns> StringBlockHash;

template <class K, class C, class A = DefaultAlloc> class Env
{
public:
  typedef ConsCell<C, A> EnvCons;
  void put(K akey, C avalue);
  C get(K akey);
  void push();
  void pop();
  void
  clear()
  {
    store.clear();
    scope.clear();
  }

  Env() {}
  Map<K, List<C> *, A> store;
  List<List<K>, A> scope;
  List<C, A> *get_bucket(K akey);
};

/* IMPLEMENTATION */

template <class K, class C, class A>
inline C
Map<K, C, A>::get(K akey)
{
  MapElem<K, C> e(akey, (C)0);
  MapElem<K, C> *x = this->set_in(e);
  if (x)
    return x->value;
  return (C)0;
}

template <class K, class C, class A>
inline C *
Map<K, C, A>::getp(K akey)
{
  MapElem<K, C> e(akey, (C)0);
  MapElem<K, C> *x = this->set_in(e);
  if (x)
    return &x->value;
  return 0;
}

template <class K, class C, class A>
inline MapElem<K, C> *
Map<K, C, A>::put(K akey, C avalue)
{
  MapElem<K, C> e(akey, avalue);
  MapElem<K, C> *x = this->set_in(e);
  if (x) {
    x->value = avalue;
    return x;
  } else
    return this->set_add(e);
}

template <class K, class C, class A>
inline MapElem<K, C> *
Map<K, C, A>::put(K akey)
{
  MapElem<K, C> e(akey, 0);
  MapElem<K, C> *x = this->set_in(e);
  if (x)
    return x;
  else
    return this->set_add(e);
}

template <class K, class C, class A>
inline void
Map<K, C, A>::get_keys(Vec<K> &keys)
{
  for (int i = 0; i < n; i++)
    if (v[i].key)
      keys.add(v[i].key);
}

template <class K, class C, class A>
inline void
Map<K, C, A>::get_keys_set(Vec<K> &keys)
{
  for (int i = 0; i < n; i++)
    if (v[i].key)
      keys.set_add(v[i].key);
}

template <class K, class C, class A>
inline void
Map<K, C, A>::get_values(Vec<C> &values)
{
  for (int i = 0; i < n; i++)
    if (v[i].key)
      values.set_add(v[i].value);
  values.set_to_vec();
}

template <class K, class C, class A>
inline void
Map<K, C, A>::map_union(Map<K, C> &m)
{
  for (int i = 0; i < m.n; i++)
    if (m.v[i].key)
      put(m.v[i].key, m.v[i].value);
}

template <class K, class C, class A>
inline bool
Map<K, C, A>::some_disjunction(Map<K, C> &m) const
{
  for (size_t i = 0; i < m.n; i++) {
    if (m.v[i].key && get(m.v[i].key) != m.v[i].value) {
      return true;
    }
  }
  for (size_t i = 0; i < n; i++) {
    if (v[i].key && m.get(v[i].key) != v[i].value) {
      return true;
    }
  }
  return false;
}

template <class K, class C, class A>
inline void
map_set_add(Map<K, Vec<C, A> *, A> &m, K akey, C avalue)
{
  Vec<C, A> *v = m.get(akey);
  if (!v)
    m.put(akey, (v = new Vec<C, A>));
  v->set_add(avalue);
}

template <class K, class C, class A>
inline void
map_set_add(Map<K, Vec<C, A> *, A> &m, K akey, Vec<C> *madd)
{
  Vec<C, A> *v = m.get(akey);
  if (!v)
    m.put(akey, (v = new Vec<C, A>));
  v->set_union(*madd);
}

template <class K, class AHashFns, class C, class A>
inline C
HashSet<K, AHashFns, C, A>::get(K akey)
{
  if (!n)
    return 0;
  if (n <= MAP_INTEGRAL_SIZE) {
    for (C *c = v; c < v + n; c++)
      if (c)
        if (AHashFns::equal(akey, *c))
          return *c;
    return 0;
  }
  uintptr_t h = AHashFns::hash(akey);
  h           = h % n;
  for (int k = h, j = 0; j < i + 3; j++) {
    if (!v[k])
      return 0;
    else if (AHashFns::equal(akey, v[k]))
      return v[k];
    k = (k + open_hash_primes[j]) % n;
  }
  return 0;
}

template <class K, class AHashFns, class C, class A>
inline C *
HashSet<K, AHashFns, C, A>::put(C avalue)
{
  if (n < MAP_INTEGRAL_SIZE) {
    if (!v)
      v = e;
    for (int i = 0; i < n; i++)
      if (AHashFns::equal(avalue, v[i]))
        return &v[i];
    v[n] = avalue;
    n++;
    return &v[n - 1];
  }
  if (n > MAP_INTEGRAL_SIZE) {
    uintptr_t h = AHashFns::hash(avalue);
    h           = h % n;
    for (int k = h, j = 0; j < i + 3; j++) {
      if (!v[k]) {
        v[k] = avalue;
        return &v[k];
      }
      k = (k + open_hash_primes[j]) % n;
    }
  } else
    i = SET_INITIAL_INDEX - 1; // will be incremented in set_expand
  HashSet<K, AHashFns, C, A> vv(*this);
  Vec<C, A>::set_expand();
  for (int i = 0; i < vv.n; i++)
    if (vv.v[i])
      put(vv.v[i]);
  return put(avalue);
}

template <class K, class AHashFns, class C, class A>
inline MapElem<K, C> *
HashMap<K, AHashFns, C, A>::get_internal(K akey)
{
  if (!n)
    return 0;
  if (n <= MAP_INTEGRAL_SIZE) {
    for (MapElem<K, C> *c = v; c < v + n; c++)
      if (c->key)
        if (AHashFns::equal(akey, c->key))
          return c;
    return 0;
  }
  uintptr_t h = AHashFns::hash(akey);
  h           = h % n;
  for (size_t k = h, j = 0; j < i + 3; j++) {
    if (!v[k].key)
      return 0;
    else if (AHashFns::equal(akey, v[k].key))
      return &v[k];
    k = (k + open_hash_primes[j]) % n;
  }
  return 0;
}

template <class K, class AHashFns, class C, class A>
inline C
HashMap<K, AHashFns, C, A>::get(K akey)
{
  MapElem<K, C> *x = get_internal(akey);
  if (!x)
    return 0;
  return x->value;
}

template <class K, class AHashFns, class C, class A>
inline MapElem<K, C> *
HashMap<K, AHashFns, C, A>::put(K akey, C avalue)
{
  MapElem<K, C> *x = get_internal(akey);
  if (x) {
    x->value = avalue;
    return x;
  } else {
    if (n < MAP_INTEGRAL_SIZE) {
      if (!v)
        v        = e;
      v[n].key   = akey;
      v[n].value = avalue;
      n++;
      return &v[n - 1];
    }
    if (n > MAP_INTEGRAL_SIZE) {
      uintptr_t h = AHashFns::hash(akey);
      h           = h % n;
      for (size_t k = h, j = 0; j < i + 3; j++) {
        if (!v[k].key) {
          v[k].key   = akey;
          v[k].value = avalue;
          return &v[k];
        }
        k = (k + open_hash_primes[j]) % n;
      }
    } else
      i = SET_INITIAL_INDEX - 1; // will be incremented in set_expand
  }
  HashMap<K, AHashFns, C, A> vv(*this);
  Map<K, C, A>::set_expand();
  for (size_t i = 0; i < vv.n; i++)
    if (vv.v[i].key)
      put(vv.v[i].key, vv.v[i].value);
  return put(akey, avalue);
}

template <class K, class AHashFns, class C, class A>
inline void
HashMap<K, AHashFns, C, A>::get_keys(Vec<K> &keys)
{
  Map<K, C, A>::get_keys(keys);
}

template <class K, class AHashFns, class C, class A>
inline void
HashMap<K, AHashFns, C, A>::get_values(Vec<C> &values)
{
  Map<K, C, A>::get_values(values);
}

template <class C, class AHashFns, class A>
C
ChainHash<C, AHashFns, A>::put(C c)
{
  uintptr_t h = AHashFns::hash(c);
  List<C, A> *l;
  MapElem<uintptr_t, List<C, A>> e(h, (C)0);
  MapElem<uintptr_t, List<C, A>> *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<C, A>, A>::put(h, c)->value;
    return l->head->car;
  }
  forc_List(ChainCons, x, *l) if (AHashFns::equal(c, x->car)) return x->car;
  l->push(c);
  return (C)0;
}

template <class C, class AHashFns, class A>
C
ChainHash<C, AHashFns, A>::get(C c)
{
  uintptr_t h = AHashFns::hash(c);
  List<C> empty;
  MapElem<uintptr_t, List<C, A>> e(h, empty);
  MapElem<uintptr_t, List<C, A>> *x = this->set_in(e);
  if (!x)
    return 0;
  List<C> *l = &x->value;
  forc_List(ChainCons, x, *l) if (AHashFns::equal(c, x->car)) return x->car;
  return 0;
}

template <class C, class AHashFns, class A>
C
ChainHash<C, AHashFns, A>::put_bag(C c)
{
  uintptr_t h = AHashFns::hash(c);
  List<C, A> *l;
  MapElem<uintptr_t, List<C, A>> e(h, (C)0);
  MapElem<uintptr_t, List<C, A>> *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<C, A>>::put(h, c)->value;
    return l->head->car;
  }
  l->push(c);
  return (C)0;
}

template <class C, class AHashFns, class A>
int
ChainHash<C, AHashFns, A>::get_bag(C c, Vec<C> &v)
{
  uintptr_t h = AHashFns::hash(c);
  List<C, A> empty;
  MapElem<uintptr_t, List<C, A>> e(h, empty);
  MapElem<uintptr_t, List<C, A>> *x = this->set_in(e);
  if (!x)
    return 0;
  List<C, A> *l = &x->value;
  forc_List(C, x, *l) if (AHashFns::equal(c, x->car)) v.add(x->car);
  return v.n;
}

template <class C, class AHashFns, class A>
void
ChainHash<C, AHashFns, A>::get_elements(Vec<C> &elements)
{
  for (int i = 0; i < n; i++) {
    List<C, A> *l = &v[i].value;
    forc_List(C, x, *l) elements.add(x);
  }
}

template <class C, class AHashFns, class A>
int
ChainHash<C, AHashFns, A>::del(C c)
{
  uintptr_t h = AHashFns::hash(c);
  List<C> *l;
  MapElem<uintptr_t, List<C, A>> e(h, (C)0);
  MapElem<uintptr_t, List<C, A>> *x = this->set_in(e);
  if (x)
    l = &x->value;
  else
    return 0;
  ConsCell<C> *last = 0;
  forc_List(ConsCell<C>, x, *l)
  {
    if (AHashFns::equal(c, x->car)) {
      if (!last)
        l->head = x->cdr;
      else
        last->cdr = x->cdr;
      A::free(x);
      return 1;
    }
    last = x;
  }
  return 0;
}

template <class K, class AHashFns, class C, class A>
MapElem<K, C> *
ChainHashMap<K, AHashFns, C, A>::put(K akey, C avalue)
{
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K, C>, A> empty;
  List<MapElem<K, C>, A> *l;
  MapElem<K, C> c(akey, avalue);
  MapElem<uintptr_t, List<MapElem<K, C>, A>> e(h, empty);
  MapElem<uintptr_t, List<MapElem<K, C>, A>> *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<MapElem<K, C>, A>, A>::put(h, c)->value;
    return &l->head->car;
  }
  for (ConsCell<MapElem<K, C>, A> *p = l->head; p; p = p->cdr)
    if (AHashFns::equal(akey, p->car.key)) {
      p->car.value = avalue;
      return &p->car;
    }
  l->push(c);
  return 0;
}

template <class K, class AHashFns, class C, class A>
C
ChainHashMap<K, AHashFns, C, A>::get(K akey)
{
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K, C>, A> empty;
  MapElem<uintptr_t, List<MapElem<K, C>, A>> e(h, empty);
  MapElem<uintptr_t, List<MapElem<K, C>, A>> *x = this->set_in(e);
  if (!x)
    return 0;
  List<MapElem<K, C>, A> *l = &x->value;
  if (l->head)
    for (ConsCell<MapElem<K, C>, A> *p = l->head; p; p = p->cdr)
      if (AHashFns::equal(akey, p->car.key))
        return p->car.value;
  return 0;
}

template <class K, class AHashFns, class C, class A>
MapElem<K, C> *
ChainHashMap<K, AHashFns, C, A>::put_bag(K akey, C avalue)
{
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K, C>, A> empty;
  List<MapElem<K, C>, A> *l;
  MapElem<K, C> c(akey, avalue);
  MapElem<uintptr_t, List<MapElem<K, C>, A>> e(h, empty);
  MapElem<uintptr_t, List<MapElem<K, C>, A>> *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<MapElem<K, C>, A>, A>::put(h, c)->value;
    return &l->head->car;
  }
  for (ConsCell<MapElem<K, C>, A> *p = l->head; p; p = p->cdr)
    if (AHashFns::equal(akey, p->car.key) && AHashFns::equal_value(avalue, p->car.value))
      return &p->car;
  l->push(c);
  return 0;
}

template <class K, class AHashFns, class C, class A>
int
ChainHashMap<K, AHashFns, C, A>::get_bag(K akey, Vec<C> &v)
{
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K, C>, A> empty;
  MapElem<uintptr_t, List<MapElem<K, C>, A>> e(h, empty);
  MapElem<uintptr_t, List<MapElem<K, C>, A>> *x = this->set_in(e);
  if (!x)
    return 0;
  List<MapElem<K, C>, A> *l = &x->value;
  for (ConsCell<MapElem<K, C>, A> *p = l->head; p; p = p->cdr)
    if (AHashFns::equal(akey, p->car.key))
      return v.add(x->car);
  return v.n;
}

template <class K, class AHashFns, class C, class A>
int
ChainHashMap<K, AHashFns, C, A>::del(K akey)
{
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K, C>, A> empty;
  List<MapElem<K, C>, A> *l;
  MapElem<uintptr_t, List<MapElem<K, C>, A>> e(h, empty);
  MapElem<uintptr_t, List<MapElem<K, C>, A>> *x = this->set_in(e);
  if (x)
    l = &x->value;
  else
    return 0;
  ConsCell<MapElem<K, C>, A> *last = 0;
  for (ConsCell<MapElem<K, C>, A> *p = l->head; p; p = p->cdr) {
    if (AHashFns::equal(akey, p->car.key)) {
      if (!last)
        l->head = p->cdr;
      else
        last->cdr = p->cdr;
      return 1;
    }
    last = p;
  }
  return 0;
}

template <class K, class AHashFns, class C, class A>
void
ChainHashMap<K, AHashFns, C, A>::get_keys(Vec<K> &keys)
{
  for (size_t i = 0; i < n; i++) {
    List<MapElem<K, C>> *l = &v[i].value;
    if (l->head)
      for (ConsCell<MapElem<K, C>, A> *p = l->head; p; p = p->cdr)
        keys.add(p->car.key);
  }
}

template <class K, class AHashFns, class C, class A>
void
ChainHashMap<K, AHashFns, C, A>::get_values(Vec<C> &values)
{
  for (size_t i = 0; i < n; i++) {
    List<MapElem<K, C>, A> *l = &v[i].value;
    if (l->head)
      for (ConsCell<MapElem<K, C>, A> *p = l->head; p; p = p->cdr)
        values.add(p->car.value);
  }
}

template <class F, class A>
inline cchar *
StringChainHash<F, A>::canonicalize(cchar *s, cchar *e)
{
  uintptr_t h = 0;
  cchar *a    = s;
  // 31 changed to 27, to avoid prime2 in vec.cpp
  if (e)
    while (a != e)
      h = h * 27 + (unsigned char)*a++;
  else
    while (*a)
      h = h * 27 + (unsigned char)*a++;
  MapElem<uintptr_t, List<cchar *, A>> me(h, (char *)0);
  MapElem<uintptr_t, List<cchar *, A>> *x = this->set_in(me);
  if (x) {
    List<cchar *, A> *l = &x->value;
    typedef ConsCell<cchar *, A> TT;
    forc_List(TT, x, *l)
    {
      a        = s;
      cchar *b = x->car;
      while (1) {
        if (!*b) {
          if (a == e)
            return x->car;
          break;
        }
        if (a >= e || *a != *b)
          break;
        a++;
        b++;
      }
    }
  }
  s         = _dupstr<A>(s, e);
  cchar *ss = ChainHash<cchar *, F, A>::put(s);
  if (ss)
    return ss;
  return s;
}

template <class K, class C, class A>
inline C
Env<K, C, A>::get(K akey)
{
  MapElem<K, List<C, A> *> e(akey, 0);
  MapElem<K, List<C, A> *> *x = store.set_in(e);
  if (x)
    return x->value->first();
  return (C)0;
}

template <class K, class C, class A>
inline List<C, A> *
Env<K, C, A>::get_bucket(K akey)
{
  List<C, A> *bucket = store.get(akey);
  if (bucket)
    return bucket;
  bucket = new List<C>();
  store.put(akey, bucket);
  return bucket;
}

template <class K, class C, class A>
inline void
Env<K, C, A>::put(K akey, C avalue)
{
  scope.head->car.push(akey);
  get_bucket(akey)->push(avalue);
}

template <class K, class C, class A>
inline void
Env<K, C, A>::push()
{
  scope.push();
}

template <class K, class C, class A>
inline void
Env<K, C, A>::pop()
{
  forc_List(EnvCons, e, scope.first()) get_bucket(e->car)->pop();
}

template <class C, class AHashFns, int N, class A> inline NBlockHash<C, AHashFns, N, A>::NBlockHash() : n(1), i(0)
{
  memset(&e[0], 0, sizeof(e));
  v = e;
}

template <class C, class AHashFns, int N, class A>
inline C *
NBlockHash<C, AHashFns, N, A>::first()
{
  return &v[0];
}

template <class C, class AHashFns, int N, class A>
inline C *
NBlockHash<C, AHashFns, N, A>::last()
{
  return &v[n * N];
}

template <class C, class AHashFns, int N, class A>
inline C
NBlockHash<C, AHashFns, N, A>::put(C c)
{
  int a;
  uintptr_t h = AHashFns::hash(c);
  C *x        = &v[(h % n) * N];
  for (a = 0; a < N; a++) {
    if (!x[a])
      break;
    if (AHashFns::equal(c, x[a]))
      return x[a];
  }
  if (a < N) {
    x[a] = c;
    return (C)0;
  }
  C *vv = first(), *ve = last();
  C *old_v = v;
  i        = i + 1;
  size(i);
  for (; vv < ve; vv++)
    if (*vv)
      put(*vv);
  if (old_v != &e[0])
    A::free(old_v);
  return put(c);
}

template <class C, class AHashFns, int N, class A>
inline void
NBlockHash<C, AHashFns, N, A>::size(int p2)
{
  n = prime2[p2];
  v = (C *)A::alloc(n * sizeof(C) * N);
  memset(v, 0, n * sizeof(C) * N);
}

template <class C, class AHashFns, int N, class A>
inline C
NBlockHash<C, AHashFns, N, A>::get(C c)
{
  if (!n)
    return (C)0;
  uintptr_t h = AHashFns::hash(c);
  C *x        = &v[(h % n) * N];
  for (int a = 0; a < N; a++) {
    if (!x[a])
      return (C)0;
    if (AHashFns::equal(c, x[a]))
      return x[a];
  }
  return (C)0;
}

template <class C, class AHashFns, int N, class A>
inline C *
NBlockHash<C, AHashFns, N, A>::assoc_get(C *c)
{
  if (!n)
    return (C *)0;
  uintptr_t h = AHashFns::hash(*c);
  C *x        = &v[(h % n) * N];
  int a       = 0;
  if (c >= x && c < x + N)
    a = c - x + 1;
  for (; a < N; a++) {
    if (!x[a])
      return (C *)0;
    if (AHashFns::equal(*c, x[a]))
      return &x[a];
  }
  return (C *)0;
}

template <class C, class AHashFns, int N, class A>
inline C *
NBlockHash<C, AHashFns, N, A>::assoc_put(C *c)
{
  int a;
  uintptr_t h = AHashFns::hash(*c);
  C *x        = &v[(h % n) * N];
  for (a = 0; a < N; a++) {
    if (!x[a])
      break;
  }
  if (a < N) {
    x[a] = *c;
    return &x[a];
  }
  x[i % N] = *c;
  i++;
  return &x[i % N];
}

template <class C, class AHashFns, int N, class A>
inline int
NBlockHash<C, AHashFns, N, A>::del(C c)
{
  int a, b;
  if (!n)
    return 0;
  uintptr_t h = AHashFns::hash(c);
  C *x        = &v[(h % n) * N];
  for (a = 0; a < N; a++) {
    if (!x[a])
      return 0;
    if (AHashFns::equal(c, x[a])) {
      if (a < N - 1) {
        for (b = a + 1; b < N; b++) {
          if (!x[b])
            break;
        }
        if (b != a + 1)
          x[a]   = x[b - 1];
        x[b - 1] = (C)0;
        return 1;
      } else {
        x[N - 1] = (C)0;
        return 1;
      }
    }
  }
  return 0;
}

template <class C, class AHashFns, int N, class A>
inline void
NBlockHash<C, AHashFns, N, A>::clear()
{
  if (v && v != e)
    A::free(v);
  v = e;
  n = 1;
}

template <class C, class AHashFns, int N, class A>
inline void
NBlockHash<C, AHashFns, N, A>::reset()
{
  if (v)
    memset(v, 0, n * N * sizeof(C));
}

template <class C, class AHashFns, int N, class A>
inline int
NBlockHash<C, AHashFns, N, A>::count()
{
  int nelements = 0;
  C *l          = last();
  for (C *xx = first(); xx < l; xx++)
    if (*xx)
      nelements++;
  return nelements;
}

template <class C, class AHashFns, int N, class A>
inline void
NBlockHash<C, AHashFns, N, A>::copy(const NBlockHash<C, AHashFns, N, A> &hh)
{
  clear();
  n = hh.n;
  i = hh.i;
  if (hh.v == &hh.e[0]) {
    memcpy(e, &hh.e[0], sizeof(e));
    v = e;
  } else {
    if (hh.v) {
      v = (C *)A::alloc(n * sizeof(C) * N);
      memcpy(v, hh.v, n * sizeof(C) * N);
    } else
      v = 0;
  }
}

template <class C, class AHashFns, int N, class A>
inline void
NBlockHash<C, AHashFns, N, A>::move(NBlockHash<C, AHashFns, N, A> &hh)
{
  clear();
  n = hh.n;
  i = hh.i;
  v = hh.v;
  if (hh.v == &hh.e[0]) {
    memcpy(e, &hh.e[0], sizeof(e));
    v = e;
  }
  hh.clear();
}

void test_map();

/* ---------------------------------------------------------------------------------------------- */
/** A hash map usable by ATS core.

    This class depends on the @c DLL class from @c List.h. It assumes it can uses instances of that
    class to store chains of elements.

    Values stored in this container are not destroyed when the container is destroyed. These must be
    released by the client.

    Duplicate keys are allowed. Clients must walk the list for multiple entries.
    @see @c Location::operator++()

    By default the table automatically expands to limit the average chain length. This can be tuned. If set
    to @c MANUAL then the table will expand @b only when explicitly requested to do so by the client.
    @see @c ExpansionPolicy
    @see @c setExpansionPolicy()
    @see @c setExpansionLimit()
    @see @c expand()

    All the parameters for the hash map are passed via the template argument @a H. This is a struct
    that contains both type definitions and static methods. It must have

    - No state (cheap and risk free to copy).

    - All required methods are static methods.

    @a ID is a @c typedef for the hash type. This is the type of the value produced by the hash function. It must be
    a numeric type.

    @a Key is a @c typedef for the key type. This is passed to the @a hash function and used for equality
    checking of elements. It is presumed cheap to copy. If the underlying key is not a simple type
    then @a Key should be declared as a constant pointer or a constant reference. The hash table
    will never attempt to modify a key.

    @a Value is a @c typedef for the value type, the type of the element stored in the hash table.

    @a ListHead is @c typedef for the @c DLL compatible class that can serve as the anchor for a chain of
    @a Value instances. This is use both as data to be stored in a bucket and for access to next and
    previous pointers from instances of @a Value.

    Method @c hash converts a @c Key to a hash value. The key argument can be by value or by constant reference.
    @code
    ID hash(Key key);
    @endcode

    Method @c key extracts the key from a @c Value instance.
    @code
    Key key(Value const*);
    @endcode

    Method @c equal checks for equality between a @c Key and a @c Value. The key argument can be a
    constant reference or by value. The arguments should be @c const if not by value.

    @code
    bool equal (Key lhs, Key rhs);
    bool equal (Key key, Value const* value);
    @endcode

    Example for @c HttpServerSession keyed by the origin server IP address.

    @code
    struct Hasher {
      typedef uint32_t ID;
      typedef sockaddr const* Key;
      typedef HttpServerSession Value;
      typedef DList(HttpServerSession, ip_hash_link) ListHead;

      static uint32_t hash(sockaddr const* key) { return ats_ip_hash(key); }
      static sockaddr const* key(HttpServerSession const* value) { return &value->ip.sa }
      static bool equal(sockaddr const* lhs, sockaddr const* rhs) { return ats_ip_eq(lhs, rhs); }
      // Alternatively
      // static ID hash(Key* key);
      // static Key key(Value* value);
      // static bool equal(Key lhs, Key rhs);
    @endcode

    In @c HttpServerSession is the definition

    @code
    LINK(HttpServerSession, ip_hash_link);
    @endcode

    which creates the internal links used by @c TSHashTable.

 */
template <typename H ///< Hashing utility class.
          >
class TSHashTable
{
public:
  typedef TSHashTable self; ///< Self reference type.

  // Make embedded types easier to use by importing them to the class namespace.
  typedef H Hasher;                           ///< Rename and promote.
  typedef typename Hasher::ID ID;             ///< ID type.
  typedef typename Hasher::Key Key;           ///< Key type.
  typedef typename Hasher::Value Value;       ///< Stored value (element) type.
  typedef typename Hasher::ListHead ListHead; ///< Anchor for chain.

  /// When the hash table is expanded.
  enum ExpansionPolicy {
    MANUAL,  ///< Client must explicitly expand the table.
    AVERAGE, ///< Table expands if average chain length exceeds limit. [default]
    MAXIMUM  ///< Table expands if any chain length exceeds limit.
  };

  /** Hash bucket.
      This is stored in the base array, anchoring the open chaining.

      @internal The default values are selected so that zero initialization is correct. Be careful if you
      change that.
  */
  struct Bucket {
    ListHead m_chain; ///< Chain of elements.
    size_t m_count;   ///< # of elements in chain.

    /** Internal chain for iteration.

        Iteration is tricky because it needs to skip over empty buckets and detect end of buckets.
        Both of these are difficult inside the iterator without excess data. So we chain the
        non-empty buckets and let the iterator walk that. This makes end detection easy and
        iteration on sparse data fast. If we make it a doubly linked list adding and removing buckets
        is very fast as well.
    */
    LINK(Bucket, m_link);

    /** Do the values in this bucket have different keys?

        @internal This can have a false positive, but that's OK, better than the expense of being
        exact.  What we want is to avoid expanding to shorten the chain if it won't help, which it
        won't if all the keys are the same.

        @internal Because we've selected the default to be @c false so we can use @c Vec which zero fills empty elements.
    */
    bool m_mixed_p;

    /// Default constructor - equivalent to zero filled.
    Bucket() : m_count(0), m_mixed_p(false) { ink_zero(m_link); }
  };

  /** Information about locating a value in the hash table.

      An instance of this returned when searching for a key in the table. It can then be used to
      check if a matching key was found, and to iterate over equivalent keys. Note this iterator
      will touch only values which have a matching key.

      @internal It's not really an iterator, although similar.
      @internal we store the ID (hashed key value) for efficiency - we can get the actual key via the
      @a m_value member.
   */
  struct Location {
    Value *m_value;    ///< The value located.
    Bucket *m_bucket;  ///< Containing bucket of value.
    ID m_id;           ///< ID (hashed key).
    size_t m_distance; ///< How many values in the chain we've gone past to get here.

    /// Default constructor - empty location.
    Location() : m_value(NULL), m_bucket(NULL), m_id(0), m_distance(0) {}
    /// Check for location being valid (referencing a value).
    bool
    isValid() const
    {
      return NULL != m_value;
    }

    /// Automatically cast to a @c Value* for convenience.
    /// @note This lets you assign the return of @c find to a @c Value*.
    /// @note This also permits the use of this class directly as a boolean expression.
    operator Value *() const { return m_value; }
    /// Dereference.
    Value &operator*() const { return *m_value; }
    /// Dereference.
    Value *operator->() const { return m_value; }
    /// Find next value with matching key (prefix).
    Location &operator++()
    {
      if (m_value)
        this->advance();
      return *this;
    }
    /// Find next value with matching key (postfix).
    Location &operator++(int)
    {
      Location zret(*this);
      if (m_value)
        this->advance();
      return zret;
    }

  protected:
    /// Move to next matching value, no checks.
    void advance();

    friend class TSHashTable;
  };

  /** Standard iterator for walking the table.
      This iterates over all elements.
      @internal Iterator is end if m_value is NULL.
   */
  struct iterator {
    Value *m_value;   ///< Current location.
    Bucket *m_bucket; ///< Current bucket;

    iterator() : m_value(0), m_bucket(0) {}
    iterator &operator++();
    Value &operator*() { return *m_value; }
    Value *operator->() { return m_value; }
    bool
    operator==(iterator const &that)
    {
      return m_bucket == that.m_bucket && m_value == that.m_value;
    }
    bool
    operator!=(iterator const &that)
    {
      return !(*this == that);
    }

  protected:
    /// Internal iterator constructor.
    iterator(Bucket *b, Value *v) : m_value(v), m_bucket(b) {}
    friend class TSHashTable;
  };

  iterator begin(); ///< First element.
  iterator end();   ///< Past last element.

  /// The default starting number of buckets.
  static size_t const DEFAULT_BUCKET_COUNT = 7; ///< POOMA.
  /// The default expansion policy limit.
  static size_t const DEFAULT_EXPANSION_LIMIT = 4; ///< Value from previous version.

  /** Constructor (also default).
      Constructs an empty table with at least @a nb buckets.
  */
  TSHashTable(size_t nb = DEFAULT_BUCKET_COUNT);

  /** Insert a value in to the table.
      The @a value must @b NOT already be in a table of this type.
      @note The value itself is put in the table, @b not a copy.
  */
  void insert(Value *value);

  /** Find a value that matches @a key.

      @note This finds the first value with a matching @a key. No other properties
      of the value are examined.

      @return The @c Location of the value. Use @c Location::isValid() to check for success.
  */
  Location find(Key key);

  /** Get a @c Location for @a value.

      This is a bit obscure but needed in certain cases. It should only be used on a @a value that
      is already known to be in the table. It just does the bucket lookup and uses that and the @a
      value to construct a @c Location that can be used with other methods. The @a m_distance value
      is not set in this case for performance reasons.
   */
  Location find(Value *value);

  /** Remove the value at @a location from the table.

      This method assumes a @a location is consistent. Be very careful if you modify a @c Location.

      @return @c true if the value was removed, @c false otherwise.
  */
  bool remove(Location const &location);

  /** Remove @b all values with @a key.

      @note This does @b not clean up the removed elements. Use carefully to avoid leaks.

      @return @c true if any value was removed, @c false otherwise.
  */
  bool remove(Key key);

  /** Remove all values from the table.

      The values are not cleaned up. The values are not touched in this method, therefore it is safe
      to destroy them first and then @c clear this table.
  */
  void clear();

  /// Get the number of elements in the table.
  size_t
  count() const
  {
    return m_count;
  }

  /// Get the number of buckets in the table.
  size_t
  bucketCount() const
  {
    return m_array.n;
  }

  /// Enable or disable expanding the table when chains are long.
  void
  setExpansionPolicy(ExpansionPolicy p)
  {
    m_expansion_policy = p;
  }
  /// Get the current expansion policy.
  ExpansionPolicy
  getExpansionPolicy() const
  {
    return m_expansion_policy;
  }
  /// Set the limit value for the expansion policy.
  void
  setExpansionLimit(size_t n)
  {
    m_expansion_limit = n;
  }
  /// Set the limit value for the expansion policy.
  size_t
  expansionLimit() const
  {
    return m_expansion_limit;
  }

  /** Expand the hash.

      Useful primarily when the expansion policy is set to @c MANUAL.
   */
  void expand();

protected:
  typedef Vec<Bucket, DefaultAlloc, 0> Array; ///< Bucket array.

  size_t m_count;                     ///< # of elements stored in the table.
  ExpansionPolicy m_expansion_policy; ///< When to exand the table.
  size_t m_expansion_limit;           ///< Limit value for expansion.
  Array m_array;                      ///< Bucket storage.
  /// Make available to nested classes statically.
  // We must reach inside the link hackery because we're in a template and
  // must use typename. Older compilers don't handle typename outside of
  // template context so if we put typename in the base definition it won't
  // work in non-template classes.
  typedef DLL<Bucket, typename Bucket::Link_m_link> BucketChain;
  /// List of non-empty buckets.
  BucketChain m_bucket_chain;

  /** Get the ID and bucket for key.
      Fills @a m_id and @a m_bucket in @a location from @a key.
  */
  void findBucket(Key key, Location &location);
};

template <typename H>
typename TSHashTable<H>::iterator
TSHashTable<H>::begin()
{
  // Get the first non-empty bucket, if any.
  Bucket *b = m_bucket_chain.head;
  return b && b->m_chain.head ? iterator(b, b->m_chain.head) : this->end();
}

template <typename H>
typename TSHashTable<H>::iterator
TSHashTable<H>::end()
{
  return iterator(0, 0);
}

template <typename H> typename TSHashTable<H>::iterator &TSHashTable<H>::iterator::operator++()
{
  if (m_value) {
    if (NULL == (m_value = ListHead::next(m_value))) {        // end of bucket, next bucket.
      if (NULL != (m_bucket = BucketChain::next(m_bucket))) { // found non-empty next bucket.
        m_value = m_bucket->m_chain.head;
        ink_assert(m_value); // if bucket is in chain, must be non-empty.
      }
    }
  }
  return *this;
}

template <typename H>
TSHashTable<H>::TSHashTable(size_t nb) : m_count(0), m_expansion_policy(AVERAGE), m_expansion_limit(DEFAULT_EXPANSION_LIMIT)
{
  if (nb) {
    int idx = 1;
    while (prime2[idx] < nb)
      ++idx;
    m_array.n = 1; // anything non-zero.
    m_array.i = idx - 1;
  }
  m_array.set_expand();
}

template <typename H>
void
TSHashTable<H>::Location::advance()
{
  Key key = Hasher::key(m_value);
  // assumes valid location with correct key, advance to next matching key or make location invalid.
  do {
    ++m_distance;
    m_value = ListHead::next(m_value);
  } while (m_value && !Hasher::equal(key, Hasher::key(m_value)));
}

template <typename H>
void
TSHashTable<H>::findBucket(Key key, Location &location)
{
  location.m_id     = Hasher::hash(key);
  location.m_bucket = &(m_array[location.m_id % m_array.n]);
}

template <typename H>
typename TSHashTable<H>::Location
TSHashTable<H>::find(Key key)
{
  Location zret;
  Value *v;

  this->findBucket(key, zret); // zret gets updated to match the bucket.
  v = zret.m_bucket->m_chain.head;
  // Search for first matching key.
  while (0 != v && !Hasher::equal(key, Hasher::key(v)))
    v          = ListHead::next(v);
  zret.m_value = v;
  return zret;
}

template <typename H>
typename TSHashTable<H>::Location
TSHashTable<H>::find(Value *value)
{
  Location zret;
  this->findBucket(Hasher::key(value), zret);
  if (zret.m_bucket->m_chain.in(value)) // just checks value links and chain head.
    zret.m_value = value;
  return zret;
}

template <typename H>
void
TSHashTable<H>::insert(Value *value)
{
  Key key        = Hasher::key(value);
  Bucket *bucket = &(m_array[Hasher::hash(key) % m_array.n]);

  // Bad client if already in a list!
  ink_assert(!bucket->m_chain.in(value));

  // Mark mixed if not already marked and we're adding a different key.
  if (!bucket->m_mixed_p && !bucket->m_chain.empty() && !Hasher::equal(key, Hasher::key(bucket->m_chain.head)))
    bucket->m_mixed_p = true;

  bucket->m_chain.push(value);
  ++m_count;
  if (1 == ++(bucket->m_count)) // not empty, put it on the non-empty list.
    m_bucket_chain.push(bucket);
  // auto expand if appropriate.
  if ((AVERAGE == m_expansion_policy && (m_count / m_array.n) > m_expansion_limit) ||
      (MAXIMUM == m_expansion_policy && bucket->m_count > m_expansion_limit && bucket->m_mixed_p))
    this->expand();
}

template <typename H>
bool
TSHashTable<H>::remove(Location const &l)
{
  bool zret = false;
  if (l.isValid()) {
    ink_assert(l.m_bucket->m_count);
    ink_assert(l.m_bucket->m_chain.head);
    l.m_bucket->m_chain.remove(l.m_value);
    --m_count;
    --(l.m_bucket->m_count);
    if (0 == l.m_bucket->m_count) // if it's now empty, take it out of the non-empty bucket chain.
      m_bucket_chain.remove(l.m_bucket);
    else if (1 == l.m_bucket->m_count) // if count drops to 1, then it's not mixed any more.
      l.m_bucket->m_mixed_p = false;
    zret                    = true;
  }
  return zret;
}

template <typename H>
bool
TSHashTable<H>::remove(Key key)
{
  Location loc = this->find(key);
  bool zret    = loc.isValid();
  while (loc.isValid()) {
    Location target(loc);
    loc.advance();
    this->remove(target);
  }
  return zret;
}

template <typename H>
void
TSHashTable<H>::clear()
{
  Bucket null_bucket;
  // Remove the values but not the actual buckets.
  for (size_t i = 0; i < m_array.n; ++i) {
    m_array[i] = null_bucket;
  }
  // Clear container data.
  m_count = 0;
  m_bucket_chain.clear();
}

template <typename H>
void
TSHashTable<H>::expand()
{
  Bucket *b                            = m_bucket_chain.head; // stash before reset.
  ExpansionPolicy org_expansion_policy = m_expansion_policy;
  Array tmp;
  tmp.move(m_array); // stash the current array here.
  // Reset to empty state.
  m_count = 0;
  m_bucket_chain.clear();

  // Because we moved the array, we have to copy back a couple of things to make
  // the expansion actually expand. How this is supposed to work without leaks or
  // mucking about in the internal is unclear to me.
  m_array.n = 1;        // anything non-zero.
  m_array.i = tmp.i;    // set the base index.
  m_array.set_expand(); // bumps array size up to next index value.

  m_expansion_policy = MANUAL; // disable any auto expand while we're expanding.
  // Move the values from the stashed array to the expanded hash.
  while (b) {
    Value *v = b->m_chain.head;
    while (v) {
      b->m_chain.remove(v); // clear local pointers to be safe.
      this->insert(v);
      v = b->m_chain.head; // next value, because previous was removed.
    }
    b = BucketChain::next(b); // these buckets are in the stashed array so pointers are still valid.
  }
  // stashed array gets cleaned up when @a tmp goes out of scope.
  m_expansion_policy = org_expansion_policy; // reset to original value.
}

/* ---------------------------------------------------------------------------------------------- */

#endif
