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
#include "ink_assert.h"
#include "List.h"
#include "Vec.h"

#define MAP_INTEGRAL_SIZE               (1 << (2))
#define MAP_INITIAL_SHIFT               ((2)+1)
#define MAP_INITIAL_SIZE                (1 << MAP_INITIAL_SHIFT)

typedef const char cchar;

template<class A>
static inline char *
_dupstr(cchar *s, cchar *e = 0) {
  int l = e ? e-s : strlen(s);
  char *ss = (char*)A::alloc(l+1);
  memcpy(ss, s, l);
  ss[l] = 0;
  return ss;
}

// Simple direct mapped Map (pointer hash table) and Environment

template <class K, class C> class MapElem {
 public:
  K     key;
  C     value;
  bool operator==(MapElem &e) { return e.key == key; }
  operator uintptr_t(void) { return (uintptr_t)(uintptr_t)key; }
  MapElem(uintptr_t x) { ink_assert(!x); key = 0; }
  MapElem(K akey, C avalue) : key(akey), value(avalue) {}
  MapElem(MapElem &e) : key(e.key), value(e.value) {}
  MapElem() : key(0) {}
};

template <class K, class C, class A = DefaultAlloc> class Map : public Vec<MapElem<K,C>, A> {
 public:
  typedef MapElem<K,C> ME;
  typedef Vec<ME,A> PType;
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
  void map_union(Map<K,C> &m);
  bool some_disjunction(Map<K,C> &m) const;
};

template <class C> class HashFns {
 public:
  static uintptr_t hash(C a);
  static int equal(C a, C b);
};

template <class K, class C> class HashSetFns {
 public:
  static uintptr_t hash(C a);
  static uintptr_t hash(K a);
  static int equal(C a, C b);
  static int equal(K a, C b);
};

template <class K, class AHashFns, class C, class A = DefaultAlloc> class HashMap : public Map<K,C,A> {
 public:
  using Map<K,C,A>::n;
  using Map<K,C,A>::i;
  using Map<K,C,A>::v;
  using Map<K,C,A>::e;
  MapElem<K,C> *get_internal(K akey);
  C get(K akey);
  MapElem<K,C> *put(K akey, C avalue);
  void get_keys(Vec<K> &keys);
  void get_values(Vec<C> &values);
};

#define form_Map(_c, _p, _v) if ((_v).n) for (_c *qq__##_p = (_c*)0, *_p = &(_v).v[0]; \
             ((uintptr_t)(qq__##_p) < (_v).n) && ((_p = &(_v).v[(uintptr_t)qq__##_p]) || 1); \
             qq__##_p = (_c*)(((uintptr_t)qq__##_p) + 1)) \
          if ((_p)->key)


template <class K, class AHashFns, class C, class A = DefaultAlloc> class HashSet : public Vec<C,A> {
 public:
  typedef Vec<C,A> V;
  using V::n;
  using V::i;
  using V::v;
  using V::e;
  C get(K akey);
  C *put( C avalue);
};

class StringHashFns {
 public:
  static uintptr_t hash(cchar *s) { 
    uintptr_t h = 0; 
    // 31 changed to 27, to avoid prime2 in vec.cpp
    while (*s) h = h * 27 + (unsigned char)*s++;  
    return h;
  }
  static int equal(cchar *a, cchar *b) { return !strcmp(a, b); }
};

class CaseStringHashFns {
 public:
  static uintptr_t hash(cchar *s) { 
    uintptr_t h = 0; 
    // 31 changed to 27, to avoid prime2 in vec.cpp
    while (*s) h = h * 27 + (unsigned char)toupper(*s++);
    return h;
  }
  static int equal(cchar *a, cchar *b) { return !strcasecmp(a, b); }
};

class PointerHashFns {
 public:
  static uintptr_t hash(void *s) { return (uintptr_t)(uintptr_t)s; }
  static int equal(void *a, void *b) { return a == b; }
};

template <class C, class AHashFns, class A = DefaultAlloc> class ChainHash : public Map<uintptr_t, List<C,A>,A> {
 public:
  using Map<uintptr_t, List<C,A>,A>::n;
  using Map<uintptr_t, List<C,A>,A>::v;
  typedef ConsCell<C, A> ChainCons;
  C put(C c);
  C get(C c);
  C put_bag(C c);
  int get_bag(C c,Vec<C> &v);
  int del(C avalue);
  void get_elements(Vec<C> &elements);
};

template <class K, class AHashFns, class C, class A = DefaultAlloc> class ChainHashMap : 
  public Map<uintptr_t, List<MapElem<K,C>,A>,A> {
 public:
  using Map<uintptr_t, List<MapElem<K,C>,A>,A>::n;
  using Map<uintptr_t, List<MapElem<K,C>,A>,A>::v;
  MapElem<K,C> *put(K akey, C avalue);
  C get(K akey);
  int del(K akey);
  MapElem<K,C> *put_bag(K akey, C c);
  int get_bag(K akey, Vec<C> &v);
  void get_keys(Vec<K> &keys);
  void get_values(Vec<C> &values);
};

template<class F = StringHashFns, class A = DefaultAlloc>
class StringChainHash : public ChainHash<cchar *, F, A> {
 public:
  cchar *canonicalize(cchar *s, cchar *e);
  cchar *canonicalize(cchar *s) { return canonicalize(s, s + strlen(s)); }
};

template <class C, class AHashFns, int N, class A = DefaultAlloc> class NBlockHash {
 public:
  int n;
  int i;
  C *v;
  C e[N];

  C* end() { return last(); }
  int length() { return N * n; }
  C *first();
  C *last();
  C put(C c);
  C get(C c);
  C* assoc_put(C *c);
  C* assoc_get(C *c);
  int del(C c);
  void clear();
  void reset();
  int count();
  void size(int p2);
  void copy(const NBlockHash<C,AHashFns,N,A> &hh);
  void move(NBlockHash<C,AHashFns,N,A> &hh);
  NBlockHash();
  NBlockHash(NBlockHash<C,AHashFns,N,A> &hh) { v = e; copy(hh); }
};

/* use forv_Vec on BlockHashes */

#define DEFAULT_BLOCK_HASH_SIZE 4
template <class C, class ABlockHashFns> class BlockHash : 
  public NBlockHash<C, ABlockHashFns, DEFAULT_BLOCK_HASH_SIZE> {};
typedef BlockHash<cchar *, StringHashFns> StringBlockHash;

template <class K, class C, class A = DefaultAlloc> class Env {
 public:
  typedef ConsCell<C, A> EnvCons;
  void put(K akey, C avalue);
  C get(K akey);
  void push();
  void pop();
  void clear() { store.clear(); scope.clear(); }

  Env() {}
  Map<K,List<C> *, A> store;
  List<List<K>, A> scope;
  List<C, A> *get_bucket(K akey);
};

/* IMPLEMENTATION */

template <class K, class C, class A> inline C 
Map<K,C,A>::get(K akey) {
  MapElem<K,C> e(akey, (C)0);
  MapElem<K,C> *x = this->set_in(e);
  if (x)
    return x->value;
  return (C)0;
}

template <class K, class C, class A> inline C *
Map<K,C,A>::getp(K akey) {
  MapElem<K,C> e(akey, (C)0);
  MapElem<K,C> *x = this->set_in(e);
  if (x)
    return &x->value;
  return 0;
}

template <class K, class C, class A> inline MapElem<K,C> *
Map<K,C,A>::put(K akey, C avalue) {
  MapElem<K,C> e(akey, avalue);
  MapElem<K,C> *x = this->set_in(e);
  if (x) {
    x->value = avalue;
    return x;
  } else
    return this->set_add(e);
}

template <class K, class C, class A> inline MapElem<K,C> *
Map<K,C,A>::put(K akey) {
  MapElem<K,C> e(akey, 0);
  MapElem<K,C> *x = this->set_in(e);
  if (x)
    return x;
  else
    return this->set_add(e);
}

template <class K, class C, class A> inline void
Map<K,C,A>::get_keys(Vec<K> &keys) {
  for (int i = 0; i < n; i++)
    if (v[i].key)
      keys.add(v[i].key);
}

template <class K, class C, class A> inline void
Map<K,C,A>::get_keys_set(Vec<K> &keys) {
  for (int i = 0; i < n; i++)
    if (v[i].key)
      keys.set_add(v[i].key);
}

template <class K, class C, class A> inline void
Map<K,C,A>::get_values(Vec<C> &values) {
  for (int i = 0; i < n; i++)
    if (v[i].key)
      values.set_add(v[i].value);
  values.set_to_vec();
}

template <class K, class C, class A> inline void
Map<K,C,A>::map_union(Map<K,C> &m) {
  for (int i = 0; i < m.n; i++)
    if (m.v[i].key)
      put(m.v[i].key, m.v[i].value);
}

template <class K, class C, class A> inline bool
Map<K,C,A>::some_disjunction(Map<K,C> &m) const {
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

template <class K, class C, class A> inline void
map_set_add(Map<K,Vec<C,A>*,A> &m, K akey, C avalue) {
  Vec<C,A> *v = m.get(akey);
  if (!v)
    m.put(akey, (v = new Vec<C,A>));
  v->set_add(avalue);
}

template <class K, class C, class A> inline void
map_set_add(Map<K,Vec<C,A>*,A> &m, K akey, Vec<C> *madd) {
  Vec<C,A> *v = m.get(akey);
  if (!v)
    m.put(akey, (v = new Vec<C,A>));
  v->set_union(*madd);
}

template <class K, class AHashFns, class C, class A> inline C
HashSet<K, AHashFns, C, A>::get(K akey) {
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
  h = h % n;
  for (int k = h, j = 0; j < i + 3;j++) {
    if (!v[k])
      return 0;
    else if (AHashFns::equal(akey, v[k]))
      return v[k];
    k = (k + open_hash_primes[j]) % n;
  }
  return 0;
}

template <class K, class AHashFns, class C, class A> inline C *
HashSet<K, AHashFns, C, A>::put(C avalue) {
  if (n < MAP_INTEGRAL_SIZE) {
    if (!v)
      v = e;
    for (int i = 0; i < n; i++)
      if (AHashFns::equal(avalue, v[i]))
        return &v[i];
    v[n] = avalue;
    n++;
    return &v[n-1];
  }
  if (n > MAP_INTEGRAL_SIZE) {
    uintptr_t h = AHashFns::hash(avalue);
    h = h % n;
    for (int k = h, j = 0; j < i + 3; j++) {
      if (!v[k]) {
        v[k] = avalue;
        return &v[k];
      }
      k = (k + open_hash_primes[j]) % n;
    }
  } else
    i = SET_INITIAL_INDEX-1; // will be incremented in set_expand
  HashSet<K,AHashFns,C,A> vv(*this);
  Vec<C,A>::set_expand();
  for (int i = 0; i < vv.n; i++)
    if (vv.v[i])
      put(vv.v[i]);
  return put(avalue);
}

template <class K, class AHashFns, class C, class A> inline MapElem<K,C> * 
HashMap<K,AHashFns,C,A>::get_internal(K akey) {
  if (!n)
    return 0;
  if (n <= MAP_INTEGRAL_SIZE) {
    for (MapElem<K,C> *c = v; c < v + n; c++)
      if (c->key)
        if (AHashFns::equal(akey, c->key))
          return c;
    return 0;
  }
  uintptr_t h = AHashFns::hash(akey);
  h = h % n;
  for (size_t k = h, j = 0; j < i + 3;j++) {
    if (!v[k].key)
      return 0;
    else if (AHashFns::equal(akey, v[k].key))
      return &v[k];
    k = (k + open_hash_primes[j]) % n;
  }
  return 0;
}

template <class K, class AHashFns, class C, class A> inline C 
HashMap<K,AHashFns,C,A>::get(K akey) {
  MapElem<K,C> *x = get_internal(akey);
  if (!x)
    return 0;
  return x->value;
}

template <class K, class AHashFns, class C, class A> inline MapElem<K,C> *
HashMap<K,AHashFns,C,A>::put(K akey, C avalue) {
  MapElem<K,C> *x = get_internal(akey);
  if (x) {
    x->value = avalue;
    return x;
  } else {
    if (n < MAP_INTEGRAL_SIZE) {
      if (!v)
        v = e;
      v[n].key = akey;
      v[n].value = avalue;
      n++;
      return &v[n-1];
    }
    if (n > MAP_INTEGRAL_SIZE) {
      uintptr_t h = AHashFns::hash(akey);
      h = h % n;
      for (size_t k = h, j = 0; j < i + 3; j++) {
        if (!v[k].key) {
          v[k].key = akey;
          v[k].value = avalue;
          return &v[k];
        }
        k = (k + open_hash_primes[j]) % n;
      }
    } else
      i = SET_INITIAL_INDEX-1; // will be incremented in set_expand
  }
  HashMap<K,AHashFns,C,A> vv(*this);
  Map<K,C,A>::set_expand();
  for (size_t i = 0; i < vv.n; i++)
    if (vv.v[i].key)
      put(vv.v[i].key, vv.v[i].value);
  return put(akey, avalue);
}

template <class K, class AHashFns, class C, class A> inline void
HashMap<K,AHashFns,C,A>::get_keys(Vec<K> &keys) { Map<K,C,A>::get_keys(keys); }

template <class K, class AHashFns, class C, class A> inline void
HashMap<K,AHashFns,C,A>::get_values(Vec<C> &values) { Map<K,C,A>::get_values(values); }

template <class C, class AHashFns, class A> C
ChainHash<C, AHashFns, A>::put(C c) {
  uintptr_t h = AHashFns::hash(c);
  List<C,A> *l;
  MapElem<uintptr_t,List<C,A> > e(h, (C)0);
  MapElem<uintptr_t,List<C,A> > *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<C,A>, A>::put(h, c)->value;
    return l->head->car;
  }
  forc_List(ChainCons, x, *l)
    if (AHashFns::equal(c, x->car))
      return x->car;
  l->push(c);
  return (C)0;
}

template <class C, class AHashFns, class A> C
ChainHash<C, AHashFns, A>::get(C c) {
  uintptr_t h = AHashFns::hash(c);
  List<C> empty;
  MapElem<uintptr_t,List<C,A> > e(h, empty);
  MapElem<uintptr_t,List<C,A> > *x = this->set_in(e);
  if (!x)
    return 0;
  List<C> *l = &x->value;
  forc_List(ChainCons, x, *l)
    if (AHashFns::equal(c, x->car))
      return x->car;
  return 0;
}

template <class C, class AHashFns, class A> C
ChainHash<C, AHashFns, A>::put_bag(C c) {
  uintptr_t h = AHashFns::hash(c);
  List<C, A> *l;
  MapElem<uintptr_t,List<C,A> > e(h, (C)0);
  MapElem<uintptr_t,List<C,A> > *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<C,A> >::put(h, c)->value;
    return l->head->car;
  }
  l->push(c);
  return (C)0;
}

template <class C, class AHashFns, class A> int
ChainHash<C, AHashFns, A>::get_bag(C c, Vec<C> &v) {
  uintptr_t h = AHashFns::hash(c);
  List<C,A> empty;
  MapElem<uintptr_t,List<C,A> > e(h, empty);
  MapElem<uintptr_t,List<C,A> > *x = this->set_in(e);
  if (!x)
    return 0;
  List<C,A> *l = &x->value;
  forc_List(C, x, *l)
    if (AHashFns::equal(c, x->car))
      v.add(x->car);
  return v.n;
}

template <class C, class AHashFns, class A> void
ChainHash<C, AHashFns, A>::get_elements(Vec<C> &elements) {
  for (int i = 0; i < n; i++) {
    List<C, A> *l = &v[i].value;
    forc_List(C, x, *l)
      elements.add(x);
  }
}

template <class C, class AHashFns, class A> int
ChainHash<C, AHashFns, A>::del(C c) {
  uintptr_t h = AHashFns::hash(c);
  List<C> *l;
  MapElem<uintptr_t,List<C,A> > e(h, (C)0);
  MapElem<uintptr_t,List<C,A> > *x = this->set_in(e);
  if (x)
    l = &x->value;
  else
    return 0;
  ConsCell<C> *last = 0;
  forc_List(ConsCell<C>, x, *l) {
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

template <class K, class AHashFns, class C, class A>  MapElem<K,C> *
ChainHashMap<K, AHashFns, C, A>::put(K akey, C avalue) {
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K,C>,A> empty;
  List<MapElem<K,C>,A> *l;
  MapElem<K, C> c(akey, avalue);
  MapElem<uintptr_t,List<MapElem<K,C>,A> > e(h, empty);
  MapElem<uintptr_t,List<MapElem<K,C>,A> > *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<MapElem<K,C>,A>,A>::put(h, c)->value;
    return &l->head->car;
  }
  for (ConsCell<MapElem<K,C>,A> *p  = l->head; p; p = p->cdr)
    if (AHashFns::equal(akey, p->car.key)) {
      p->car.value = avalue;
      return &p->car;
    }
  l->push(c);
  return 0;
}

template <class K, class AHashFns, class C, class A> C
ChainHashMap<K, AHashFns, C, A>::get(K akey) {
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K,C>, A> empty;
  MapElem<uintptr_t,List<MapElem<K,C>,A> > e(h, empty);
  MapElem<uintptr_t,List<MapElem<K,C>,A> > *x = this->set_in(e);
  if (!x)
    return 0;
  List<MapElem<K,C>,A> *l = &x->value;
  if (l->head) 
    for (ConsCell<MapElem<K,C>,A> *p  = l->head; p; p = p->cdr)
      if (AHashFns::equal(akey, p->car.key))
        return p->car.value;
  return 0;
}

template <class K, class AHashFns, class C, class A> MapElem<K,C> *
ChainHashMap<K, AHashFns, C, A>::put_bag(K akey, C avalue) {
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K,C>,A> empty;
  List<MapElem<K,C>,A> *l;
  MapElem<K, C> c(akey, avalue);
  MapElem<uintptr_t,List<MapElem<K,C>,A> > e(h, empty);
  MapElem<uintptr_t,List<MapElem<K,C>,A> > *x = this->set_in(e);
  if (x)
    l = &x->value;
  else {
    l = &Map<uintptr_t, List<MapElem<K,C>,A>,A>::put(h, c)->value;
    return &l->head->car;
  }
  for (ConsCell<MapElem<K,C>,A> *p  = l->head; p; p = p->cdr)
    if (AHashFns::equal(akey, p->car.key) && AHashFns::equal_value(avalue, p->car.value))
      return &p->car;
  l->push(c);
  return 0;
}

template <class K, class AHashFns, class C, class A> int
ChainHashMap<K, AHashFns, C, A>::get_bag(K akey, Vec<C> &v) {
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K,C>,A> empty;
  MapElem<uintptr_t,List<MapElem<K,C>,A> > e(h, empty);
  MapElem<uintptr_t,List<MapElem<K,C>,A> > *x = this->set_in(e);
  if (!x)
    return 0;
  List<MapElem<K,C>,A> *l = &x->value;
  for (ConsCell<MapElem<K,C>,A> *p  = l->head; p; p = p->cdr)
    if (AHashFns::equal(akey, p->car.key))
      return v.add(x->car);
  return v.n;
}

template <class K, class AHashFns, class C, class A> int
ChainHashMap<K, AHashFns, C, A>::del(K akey) {
  uintptr_t h = AHashFns::hash(akey);
  List<MapElem<K,C>,A> empty;
  List<MapElem<K,C>,A> *l;
  MapElem<uintptr_t,List<MapElem<K,C>,A> > e(h, empty);
  MapElem<uintptr_t,List<MapElem<K,C>,A> > *x = this->set_in(e);
  if (x)
    l = &x->value;
  else
    return 0;
  ConsCell<MapElem<K,C>,A> *last = 0;
  for (ConsCell<MapElem<K,C>,A> *p = l->head; p; p = p->cdr) {
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

template <class K, class AHashFns, class C, class A> void
ChainHashMap<K, AHashFns, C, A>::get_keys(Vec<K> &keys) {
  for (size_t i = 0; i < n; i++) {
    List<MapElem<K,C> > *l = &v[i].value;
    if (l->head) 
      for (ConsCell<MapElem<K,C>,A> *p  = l->head; p; p = p->cdr)
        keys.add(p->car.key);
  }
}

template <class K, class AHashFns, class C, class A> void
ChainHashMap<K, AHashFns, C, A>::get_values(Vec<C> &values) {
  for (size_t i = 0; i < n; i++) {
    List<MapElem<K,C>,A> *l = &v[i].value;
    if (l->head) 
      for (ConsCell<MapElem<K,C>,A> *p  = l->head; p; p = p->cdr)
        values.add(p->car.value);
  }
}

template <class F, class A> inline cchar *
StringChainHash<F,A>::canonicalize(cchar *s, cchar *e) {
  uintptr_t h = 0;
  cchar *a = s;
  // 31 changed to 27, to avoid prime2 in vec.cpp
  if (e)
    while (a != e) h = h * 27 + (unsigned char)*a++;  
  else
    while (*a) h = h * 27 + (unsigned char)*a++;  
  MapElem<uintptr_t,List<cchar*, A> > me(h, (char*)0);
  MapElem<uintptr_t,List<cchar*, A> > *x = this->set_in(me);
  if (x) {
    List<cchar*, A> *l = &x->value;
    typedef ConsCell<cchar *, A> TT;
    forc_List(TT, x, *l) {
      a = s;
      cchar *b = x->car;
      while (1) {
        if (!*b) {
          if (a == e)
            return x->car;
          break;
        }
        if (a >= e || *a != *b)
          break;
        a++; b++;
      }
    }
  }
  s = _dupstr<A>(s, e);
  cchar *ss = ChainHash<cchar *, F, A>::put(s);
  if (ss)
    return ss;
  return s;
}

template <class K, class C, class A> inline C 
Env<K,C,A>::get(K akey) {
  MapElem<K,List<C, A> *> e(akey, 0);
  MapElem<K,List<C, A> *> *x = store.set_in(e);
  if (x)
    return x->value->first();
  return (C)0;
}

template <class K, class C, class A> inline List<C, A> *
Env<K,C,A>::get_bucket(K akey) {
  List<C, A> *bucket = store.get(akey);
  if (bucket)
    return bucket;
  bucket = new List<C>();
  store.put(akey, bucket);
  return bucket;
}

template <class K, class C, class A> inline void
Env<K,C,A>::put(K akey, C avalue) {
  scope.head->car.push(akey);
  get_bucket(akey)->push(avalue);
}

template <class K, class C, class A> inline void
Env<K,C,A>::push() {
  scope.push();
}

template <class K, class C, class A> inline void
Env<K,C,A>::pop() {
  forc_List(EnvCons, e, scope.first())
    get_bucket(e->car)->pop();
}

template <class C, class AHashFns, int N, class A> inline 
NBlockHash<C, AHashFns, N, A>::NBlockHash() : n(1), i(0) {
  memset(&e[0], 0, sizeof(e));
  v = e;
}

template <class C, class AHashFns, int N, class A> inline C*
NBlockHash<C, AHashFns, N, A>::first() {
  return &v[0];
}

template <class C, class AHashFns, int N, class A> inline C*
NBlockHash<C, AHashFns, N, A>::last() {
  return &v[n * N];
}

template <class C, class AHashFns, int N, class A> inline C
NBlockHash<C, AHashFns, N, A>::put(C c) {
  int a;
  uintptr_t h = AHashFns::hash(c);
  C *x = &v[(h % n) * N];
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
  i = i + 1;
  size(i);
  for (;vv < ve; vv++)
    if (*vv)
      put(*vv);
  if (old_v != &e[0])
    A::free(old_v);
  return put(c);
}

template <class C, class AHashFns, int N, class A> inline void
NBlockHash<C, AHashFns, N, A>::size(int p2) {
  n = prime2[p2];
  v = (C*)A::alloc(n * sizeof(C) * N);
  memset(v, 0, n * sizeof(C) * N);
}

template <class C, class AHashFns, int N, class A> inline C
NBlockHash<C, AHashFns, N, A>::get(C c) {
  if (!n)
    return (C)0;
  uintptr_t h = AHashFns::hash(c);
  C *x = &v[(h % n) * N];
  for (int a = 0; a < N; a++) {
    if (!x[a])
      return (C)0;
    if (AHashFns::equal(c, x[a]))
      return x[a];
  }
  return (C)0;
}

template <class C, class AHashFns, int N, class A> inline C*
NBlockHash<C, AHashFns, N, A>::assoc_get(C *c) {
  if (!n)
    return (C*)0;
  uintptr_t h = AHashFns::hash(*c);
  C *x = &v[(h % n) * N];
  int a = 0;
  if (c >= x && c < x + N)
    a = c - x + 1;
  for (; a < N; a++) {
    if (!x[a])
      return (C*)0;
    if (AHashFns::equal(*c, x[a]))
      return &x[a];
  }
  return (C*)0;
}

template <class C, class AHashFns, int N, class A> inline C*
NBlockHash<C, AHashFns, N, A>::assoc_put(C *c) {
  int a;
  uintptr_t h = AHashFns::hash(*c);
  C *x = &v[(h % n) * N];
  for (a = 0; a < N; a++) {
    if (!x[a])
      break;
  }
  if (a < N) {
    x[a] = *c;
    return  &x[a];
  }
  x[i % N] = *c;
  i++;
  return &x[i % N];
}

template <class C, class AHashFns, int N, class A> inline int
NBlockHash<C, AHashFns, N, A>::del(C c) {
  int a, b;
  if (!n)
    return 0;
  uintptr_t h = AHashFns::hash(c);
  C *x = &v[(h % n) * N];
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
          x[a] = x[b - 1];
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

template <class C, class AHashFns, int N, class A> inline void
NBlockHash<C, AHashFns, N, A>::clear() {
  if (v && v != e) A::free(v);
  v = e;
  n = 1;
}

template <class C, class AHashFns, int N, class A> inline void
NBlockHash<C, AHashFns, N, A>::reset() {
  if (v)
    memset(v, 0, n * N * sizeof(C));
}

template <class C, class AHashFns, int N, class A> inline int
NBlockHash<C, AHashFns, N, A>::count() {
  int nelements = 0;
  C *l = last();
  for (C *xx = first(); xx < l; xx++) 
    if (*xx)
      nelements++;
  return nelements;
}

template <class C, class AHashFns, int N, class A> inline void 
NBlockHash<C, AHashFns, N, A>::copy(const NBlockHash<C, AHashFns, N, A> &hh) {
  clear();
  n = hh.n;
  i = hh.i;
  if (hh.v == &hh.e[0]) { 
    memcpy(e, &hh.e[0], sizeof(e));
    v = e;
  } else {
    if (hh.v) {
      v = (C*)A::alloc(n * sizeof(C) * N);
      memcpy(v, hh.v, n * sizeof(C) * N);
    } else
      v = 0;
  }
}

template <class C, class AHashFns, int N, class A> inline void 
NBlockHash<C, AHashFns, N, A>::move(NBlockHash<C, AHashFns, N, A> &hh) {
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

#endif
