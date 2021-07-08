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

#pragma once

#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>

#include "tscore/defalloc.h"
#include "tscore/ink_assert.h"
#include "tscore/Diags.h"

#include "tscore/List.h"

#define MAP_INTEGRAL_SIZE (1 << (2))
//#define MAP_INITIAL_SHIFT               ((2)+1)
//#define MAP_INITIAL_SIZE                (1 << MAP_INITIAL_SHIFT)

// Simple Vector class, also supports open hashed sets
#define VEC_INTEGRAL_SHIFT_DEFAULT 2 /* power of 2 (1 << VEC_INTEGRAL_SHIFT)*/
#define VEC_INTEGRAL_SIZE (1 << (S))
#define VEC_INITIAL_SHIFT ((S) + 1)
#define VEC_INITIAL_SIZE (1 << VEC_INITIAL_SHIFT)

#define SET_LINEAR_SIZE 4 /* must be <= than VEC_INTEGRAL_SIZE */
#define SET_INITIAL_INDEX 2

template <class C, class A = DefaultAlloc, int S = VEC_INTEGRAL_SHIFT_DEFAULT> // S must be a power of 2
class Vec
{
public:
  size_t n;
  size_t i; // size index for sets, reserve for vectors
  C *v;
  C e[VEC_INTEGRAL_SIZE];

  Vec();
  Vec<C, A, S>(const Vec<C, A, S> &vv);
  Vec<C, A, S>(const C c);
  ~Vec();

  C &operator[](int i) const { return v[i]; }
  C get(size_t i) const;
  void add(C a);
  void
  push_back(C a)
  {
    add(a);
  } // std::vector name
  bool add_exclusive(C a);
  C &add();
  void drop();
  C pop();
  void reset();
  void clear();
  void free_and_clear();
  void delete_and_clear();
  void set_clear();
  C *set_add(C a);
  void set_remove(C a); // expensive, use BlockHash for cheaper remove
  C *set_add_internal(C a);
  bool set_union(Vec<C, A, S> &v);
  int set_intersection(Vec<C, A, S> &v);
  int some_intersection(Vec<C, A, S> &v);
  int some_disjunction(Vec<C, A, S> &v);
  int some_difference(Vec<C, A, S> &v);
  void set_intersection(Vec<C, A, S> &v, Vec<C, A, S> &result);
  void set_disjunction(Vec<C, A, S> &v, Vec<C, A, S> &result);
  void set_difference(Vec<C, A, S> &v, Vec<C, A, S> &result);
  size_t set_count() const;
  size_t count() const;
  C *in(C a);
  C *set_in(C a);
  C first_in_set();
  C *set_in_internal(C a);
  void set_expand();
  ssize_t index(C a) const;
  void set_to_vec();
  void vec_to_set();
  void move(Vec<C, A, S> &v);
  void copy(const Vec<C, A, S> &v);
  void fill(size_t n);
  void append(const Vec<C> &v);
  template <typename CountType> void append(const C *src, CountType count);
  void prepend(const Vec<C> &v);
  void remove_index(int index);
  void
  remove(C a)
  {
    int i = index(a);
    if (i >= 0)
      remove_index(i);
  }
  C &insert(size_t index);
  void insert(size_t index, Vec<C> &vv);
  void insert(size_t index, C a);
  void
  push(C a)
  {
    insert(0, a);
  }
  void reverse();
  void reserve(size_t n);
  C *
  end() const
  {
    return v + n;
  }
  C &
  first() const
  {
    return v[0];
  }
  C &
  last() const
  {
    return v[n - 1];
  }
  Vec<C, A, S> &
  operator=(Vec<C, A, S> &v)
  {
    this->copy(v);
    return *this;
  }
  unsigned
  length() const
  {
    return n;
  }
  // vector::size() intentionally not implemented because it should mean "bytes" not count of elements
  int write(int fd);
  int read(int fd);
  void qsort(bool (*lt)(C, C));
  void qsort(bool (*lt)(const C &, const C &));
  static void swap(C *p1, C *p2);

private:
  void move_internal(Vec<C, A, S> &v);
  void copy_internal(const Vec<C, A, S> &v);
  void add_internal(C a);
  C &add_internal();
  void addx();
};

// c -- class, p -- pointer to elements of v, v -- vector
#define forv_Vec(_c, _p, _v)                                                                \
  if ((_v).n)                                                                               \
    for (_c *qq__##_p = (_c *)0, *_p = (_v).v[0];                                           \
         ((uintptr_t)(qq__##_p) < (_v).length()) && ((_p = (_v).v[(intptr_t)qq__##_p]), 1); \
         qq__##_p = (_c *)(((intptr_t)qq__##_p) + 1))
#define for_Vec(_c, _p, _v)                                                                 \
  if ((_v).n)                                                                               \
    for (_c *qq__##_p = (_c *)0, _p = (_v).v[0];                                            \
         ((uintptr_t)(qq__##_p) < (_v).length()) && ((_p = (_v).v[(intptr_t)qq__##_p]), 1); \
         qq__##_p = (_c *)(((intptr_t)qq__##_p) + 1))
#define forvp_Vec(_c, _p, _v)                                                                \
  if ((_v).n)                                                                                \
    for (_c *qq__##_p = (_c *)0, *_p = &(_v).v[0];                                           \
         ((uintptr_t)(qq__##_p) < (_v).length()) && ((_p = &(_v).v[(intptr_t)qq__##_p]), 1); \
         qq__##_p = (_c *)(((intptr_t)qq__##_p) + 1))

template <class C, class A = DefaultAlloc, int S = VEC_INTEGRAL_SHIFT_DEFAULT> class Accum
{
public:
  Vec<C, A, S> asset;
  Vec<C, A, S> asvec;
  void
  add(C c)
  {
    if (asset.set_add(c))
      asvec.add(c);
  }
  void
  add(Vec<C, A, S> v)
  {
    for (int i = 0; i < v.n; i++)
      if (v.v[i])
        add(v.v[i]);
  }
  void
  clear()
  {
    asset.clear();
    asvec.clear();
  }
};

const uintptr_t prime2[] = {1,       3,       7,       13,       31,       61,       127,       251,       509,      1021,
                            2039,    4093,    8191,    16381,    32749,    65521,    131071,    262139,    524287,   1048573,
                            2097143, 4194301, 8388593, 16777213, 33554393, 67108859, 134217689, 268435399, 536870909};

// primes generated with map_mult.c
const uintptr_t open_hash_primes[256] = {
  0x02D4AF27, 0x1865DFC7, 0x47C62B43, 0x35B4889B, 0x210459A1, 0x3CC51CC7, 0x02ADD945, 0x0607C4D7, 0x558E6035, 0x0554224F,
  0x5A281657, 0x1C458C7F, 0x7F8BE723, 0x20B9BA99, 0x7218AA35, 0x64B10C2B, 0x548E8983, 0x5951218F, 0x7AADC871, 0x695FA5B1,
  0x40D40FCB, 0x20E03CC9, 0x55E9920F, 0x554CE08B, 0x7E78B1D7, 0x7D965DF9, 0x36A520A1, 0x1B0C6C11, 0x33385667, 0x2B0A7B9B,
  0x0F35AE23, 0x0BD608FB, 0x2284ADA3, 0x6E6C0687, 0x129B3EED, 0x7E86289D, 0x1143C24B, 0x1B6C7711, 0x1D87BB41, 0x4C7E635D,
  0x67577999, 0x0A0113C5, 0x6CF085B5, 0x14A4D0FB, 0x4E93E3A7, 0x5C87672B, 0x67F3CA17, 0x5F944339, 0x4C16DFD7, 0x5310C0E3,
  0x2FAD1447, 0x4AFB3187, 0x08468B7F, 0x49E56C51, 0x6280012F, 0x097D1A85, 0x34CC9403, 0x71028BD7, 0x6DEDC7E9, 0x64093291,
  0x6D78BB0B, 0x7A03B465, 0x2E044A43, 0x1AE58515, 0x23E495CD, 0x46102A83, 0x51B78A59, 0x051D8181, 0x5352CAC9, 0x57D1312B,
  0x2726ED57, 0x2E6BC515, 0x70736281, 0x5938B619, 0x0D4B6ACB, 0x44AB5E2B, 0x0029A485, 0x002CE54F, 0x075B0591, 0x3EACFDA9,
  0x0AC03411, 0x53B00F73, 0x2066992D, 0x76E72223, 0x55F62A8D, 0x3FF92EE1, 0x17EE0EB3, 0x5E470AF1, 0x7193EB7F, 0x37A2CCD3,
  0x7B44F7AF, 0x0FED8B3F, 0x4CC05805, 0x7352BF79, 0x3B61F755, 0x523CF9A3, 0x1AAFD219, 0x76035415, 0x5BE84287, 0x6D598909,
  0x456537E9, 0x407EA83F, 0x23F6FFD5, 0x60256F39, 0x5D8EE59F, 0x35265CEB, 0x1D4AD4EF, 0x676E2E0F, 0x2D47932D, 0x776BB33B,
  0x6DE1902B, 0x2C3F8741, 0x5B2DE8EF, 0x686DDB3B, 0x1D7C61C7, 0x1B061633, 0x3229EA51, 0x7FCB0E63, 0x5F22F4C9, 0x517A7199,
  0x2A8D7973, 0x10DCD257, 0x41D59B27, 0x2C61CA67, 0x2020174F, 0x71653B01, 0x2FE464DD, 0x3E7ED6C7, 0x164D2A71, 0x5D4F3141,
  0x5F7BABA7, 0x50E1C011, 0x140F5D77, 0x34E80809, 0x04AAC6B3, 0x29C42BAB, 0x08F9B6F7, 0x461E62FD, 0x45C2660B, 0x08BF25A7,
  0x5494EA7B, 0x0225EBB7, 0x3C5A47CF, 0x2701C333, 0x457ED05B, 0x48CDDE55, 0x14083099, 0x7C69BDAB, 0x7BF163C9, 0x41EE1DAB,
  0x258B1307, 0x0FFAD43B, 0x6601D767, 0x214DBEC7, 0x2852CCF5, 0x0009B471, 0x190AC89D, 0x5BDFB907, 0x15D4E331, 0x15D22375,
  0x13F388D5, 0x12ACEDA5, 0x3835EA5D, 0x2587CA35, 0x06756643, 0x487C6F55, 0x65C295EB, 0x1029F2E1, 0x10CEF39D, 0x14C2E415,
  0x444825BB, 0x24BE0A2F, 0x1D2B7C01, 0x64AE3235, 0x5D2896E5, 0x61BBBD87, 0x4A49E86D, 0x12C277FF, 0x72C81289, 0x5CF42A3D,
  0x332FF177, 0x0DAECD23, 0x6000ED1D, 0x203CDDE1, 0x40C62CAD, 0x19B9A855, 0x782020C3, 0x6127D5BB, 0x719889A7, 0x40E4FCCF,
  0x2A3C8FF9, 0x07411C7F, 0x3113306B, 0x4D7CA03F, 0x76119841, 0x54CEFBDF, 0x11548AB9, 0x4B0748EB, 0x569966B1, 0x45BC721B,
  0x3D5A376B, 0x0D8923E9, 0x6D95514D, 0x0F39A367, 0x2FDAD92F, 0x721F972F, 0x42D0E21D, 0x5C5952DB, 0x7394D007, 0x02692C55,
  0x7F92772F, 0x025F8025, 0x34347113, 0x560EA689, 0x0DCC21DF, 0x09ECC7F5, 0x091F3993, 0x0E0B52AB, 0x497CAA55, 0x0A040A49,
  0x6D8F0CC5, 0x54F41609, 0x6E0CB8DF, 0x3DCB64C3, 0x16C365CD, 0x6D6B9FB5, 0x02B9382B, 0x6A5BFAF1, 0x1669D75F, 0x13CFD4FD,
  0x0FDF316F, 0x21F3C463, 0x6FC58ABF, 0x04E45BE7, 0x1911225B, 0x28CD1355, 0x222084E9, 0x672AD54B, 0x476FC267, 0x6864E16D,
  0x20AEF4FB, 0x603C5FB9, 0x55090595, 0x1113B705, 0x24E38493, 0x5291AF97, 0x5F5446D9, 0x13A6F639, 0x3D501313, 0x37E02017,
  0x236B0ED3, 0x60F246BF, 0x01E02501, 0x2D2F66BD, 0x6BF23609, 0x16729BAF};

/* IMPLEMENTATION */

template <class C, class A, int S> inline Vec<C, A, S>::Vec() : n(0), i(0), v(nullptr)
{
  memset(static_cast<void *>(&e[0]), 0, sizeof(e));
}

template <class C, class A, int S> inline Vec<C, A, S>::Vec(const Vec<C, A, S> &vv)
{
  copy(vv);
}

template <class C, class A, int S> inline Vec<C, A, S>::Vec(C c)
{
  n    = 1;
  i    = 0;
  v    = &e[0];
  e[0] = c;
}

template <class C, class A, int S>
inline C
Vec<C, A, S>::get(size_t i) const
{
  if (i < n) {
    return v[i];
  } else {
    return C();
  }
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::add(C a)
{
  if (n & (VEC_INTEGRAL_SIZE - 1))
    v[n++] = a;
  else if (!v)
    (v = e)[n++] = a;
  else
    add_internal(a);
}

template <class C, class A, int S>
inline C &
Vec<C, A, S>::add()
{
  C *ret;
  if (n & (VEC_INTEGRAL_SIZE - 1))
    ret = &v[n++];
  else if (!v)
    ret = &(v = e)[n++];
  else
    ret = &add_internal();
  return *ret;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::drop()
{
  if (n && 0 == --n)
    clear();
}

template <class C, class A, int S>
inline C
Vec<C, A, S>::pop()
{
  if (!n)
    return 0;
  n--;
  C ret = v[n];
  if (!n)
    clear();
  return ret;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::set_clear()
{
  memset(v, 0, n * sizeof(C));
}

template <class C, class A, int S>
inline C *
Vec<C, A, S>::set_add(C a)
{
  if (n < SET_LINEAR_SIZE) {
    for (C *c = v; c < v + n; c++)
      if (*c == a)
        return nullptr;
    add(a);
    return &v[n - 1];
  }
  if (n == SET_LINEAR_SIZE) {
    Vec<C, A, S> vv(*this);
    clear();
    for (C *c = vv.v; c < vv.v + vv.n; c++) {
      set_add_internal(*c);
    }
  }
  return set_add_internal(a);
}

template <class C, class A, int S>
void
Vec<C, A, S>::set_remove(C a)
{
  Vec<C, A, S> tmp;
  tmp.move(*this);
  for (C *c = tmp.v; c < tmp.v + tmp.n; c++)
    if (*c != a)
      set_add(a);
}

template <class C, class A, int S>
inline size_t
Vec<C, A, S>::count() const
{
  int x = 0;
  for (C *c = v; c < v + n; c++)
    if (*c)
      x++;
  return x;
}

template <class C, class A, int S>
inline C *
Vec<C, A, S>::in(C a)
{
  for (C *c = v; c < v + n; c++)
    if (*c == a)
      return c;
  return nullptr;
}

template <class C, class A, int S>
inline bool
Vec<C, A, S>::add_exclusive(C a)
{
  if (!in(a)) {
    add(a);
    return true;
  } else
    return false;
}

template <class C, class A, int S>
inline C *
Vec<C, A, S>::set_in(C a)
{
  if (n <= SET_LINEAR_SIZE)
    return in(a);
  return set_in_internal(a);
}

template <class C, class A, int S>
inline C
Vec<C, A, S>::first_in_set()
{
  for (C *c = v; c < v + n; c++)
    if (*c)
      return *c;
  return 0;
}

template <class C, class A, int S>
inline ssize_t
Vec<C, A, S>::index(C a) const
{
  for (C *c = v; c < v + n; c++) {
    if (*c == a) {
      return c - v;
    }
  }
  return -1;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::move_internal(Vec<C, A, S> &vv)
{
  n = vv.n;
  i = vv.i;
  if (vv.v == &vv.e[0]) {
    memcpy(static_cast<void *>(e), &vv.e[0], sizeof(e));
    v = e;
  } else
    v = vv.v;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::move(Vec<C, A, S> &vv)
{
  move_internal(vv);
  vv.v = nullptr;
  vv.clear();
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::copy(const Vec<C, A, S> &vv)
{
  n = vv.n;
  i = vv.i;
  if (vv.v == &vv.e[0]) {
    memcpy(static_cast<void *>(e), &vv.e[0], sizeof(e));
    v = e;
  } else {
    if (vv.v)
      copy_internal(vv);
    else
      v = nullptr;
  }
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::fill(size_t nn)
{
  for (size_t i = n; i < nn; i++)
    add() = 0;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::append(const Vec<C> &vv)
{
  for (C *c = vv.v; c < vv.v + vv.n; c++)
    if (*c != 0)
      add(*c);
}

template <class C, class A, int S>
template <typename CountType>
inline void
Vec<C, A, S>::append(const C *src, CountType count)
{
  reserve(length() + count);
  for (CountType c = 0; c < count; ++c) {
    add(src[c]);
  }
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::prepend(const Vec<C> &vv)
{
  if (vv.n) {
    int oldn = n;
    fill(n + vv.n);
    if (oldn)
      memmove(&v[vv.n], &v[0], oldn * sizeof(v[0]));
    memcpy(&v[0], vv.v, vv.n * sizeof(v[0]));
  }
}

template <class C, class A, int S>
void
Vec<C, A, S>::add_internal(C a)
{
  addx();
  v[n++] = a;
}

template <class C, class A, int S>
C &
Vec<C, A, S>::add_internal()
{
  addx();
  return v[n++];
}

template <class C, class A, int S>
C *
Vec<C, A, S>::set_add_internal(C c)
{
  size_t j, k;
  if (n) {
    uintptr_t h = (uintptr_t)c;
    h           = h % n;
    for (k = h, j = 0; j < i + 3; j++) {
      if (!v[k]) {
        v[k] = c;
        return &v[k];
      } else if (v[k] == c) {
        return nullptr;
      }
      k = (k + open_hash_primes[j]) % n;
    }
  }
  Vec<C, A, S> vv;
  vv.move_internal(*this);
  set_expand();
  if (vv.v) {
    set_union(vv);
  }
  return set_add(c);
}

template <class C, class A, int S>
C *
Vec<C, A, S>::set_in_internal(C c)
{
  size_t j, k;
  if (n) {
    uintptr_t h = (uintptr_t)c;
    h           = h % n;
    for (k = h, j = 0; j < i + 3; j++) {
      if (!v[k])
        return nullptr;
      else if (v[k] == c)
        return &v[k];
      k = (k + open_hash_primes[j]) % n;
    }
  }
  return nullptr;
}

template <class C, class A, int S>
bool
Vec<C, A, S>::set_union(Vec<C, A, S> &vv)
{
  bool changed = false;
  for (size_t i = 0; i < vv.n; i++) {
    if (vv.v[i]) {
      changed = set_add(vv.v[i]) || changed;
    }
  }
  return changed;
}

template <class C, class A, int S>
int
Vec<C, A, S>::set_intersection(Vec<C, A, S> &vv)
{
  Vec<C, A, S> tv;
  tv.move(*this);
  int changed = 0;
  for (int i = 0; i < tv.n; i++)
    if (tv.v[i]) {
      if (vv.set_in(tv.v[i]))
        set_add(tv.v[i]);
      else
        changed = 1;
    }
  return changed;
}

template <class C, class A, int S>
int
Vec<C, A, S>::some_intersection(Vec<C, A, S> &vv)
{
  for (int i = 0; i < n; i++)
    if (v[i])
      if (vv.set_in(v[i]))
        return 1;
  return 0;
}

template <class C, class A, int S>
int
Vec<C, A, S>::some_disjunction(Vec<C, A, S> &vv)
{
  for (int i = 0; i < n; i++)
    if (v[i])
      if (!vv.set_in(v[i]))
        return 1;
  for (int i = 0; i < vv.n; i++)
    if (vv.v[i])
      if (!set_in(vv.v[i]))
        return 1;
  return 0;
}

template <class C, class A, int S>
void
Vec<C, A, S>::set_intersection(Vec<C, A, S> &vv, Vec<C, A, S> &result)
{
  for (int i = 0; i < n; i++)
    if (v[i])
      if (vv.set_in(v[i]))
        result.set_add(v[i]);
}

template <class C, class A, int S>
void
Vec<C, A, S>::set_disjunction(Vec<C, A, S> &vv, Vec<C, A, S> &result)
{
  for (int i = 0; i < n; i++)
    if (v[i])
      if (!vv.set_in(v[i]))
        result.set_add(v[i]);
  for (int i = 0; i < vv.n; i++)
    if (vv.v[i])
      if (!set_in(vv.v[i]))
        result.set_add(vv.v[i]);
}

template <class C, class A, int S>
void
Vec<C, A, S>::set_difference(Vec<C, A, S> &vv, Vec<C, A, S> &result)
{
  for (int i = 0; i < n; i++)
    if (v[i])
      if (!vv.set_in(v[i]))
        result.set_add(v[i]);
}

template <class C, class A, int S>
int
Vec<C, A, S>::some_difference(Vec<C, A, S> &vv)
{
  for (int i = 0; i < n; i++)
    if (v[i])
      if (!vv.set_in(v[i]))
        return 1;
  return 0;
}

template <class C, class A, int S>
size_t
Vec<C, A, S>::set_count() const
{
  size_t x = 0;
  for (size_t i = 0; i < n; i++) {
    if (v[i]) {
      x++;
    }
  }
  return x;
}

template <class C, class A, int S>
void
Vec<C, A, S>::set_to_vec()
{
  C *x = &v[0], *y = x;
  for (; y < v + n; y++) {
    if (*y) {
      if (x != y)
        *x = *y;
      x++;
    }
  }
  if (i) {
    i = prime2[i]; // convert set allocation to reserve
    if (i - n > 0)
      memset(&v[n], 0, (i - n) * (sizeof(C)));
  } else {
    i = 0;
    if (v == &e[0] && VEC_INTEGRAL_SIZE - n > 0)
      memset(&v[n], 0, (VEC_INTEGRAL_SIZE - n) * (sizeof(C)));
  }
}

template <class C, class A, int S>
void
Vec<C, A, S>::vec_to_set()
{
  Vec<C, A, S> vv;
  vv.move(*this);
  for (C *c = vv.v; c < vv.v + vv.n; c++)
    set_add(*c);
}

template <class C, class A, int S>
void
Vec<C, A, S>::remove_index(int index)
{
  if (n > 1)
    memmove(&v[index], &v[index + 1], (n - 1 - index) * sizeof(v[0]));
  n--;
  if (n <= 0)
    v = e;
}

template <class C, class A, int S>
void
Vec<C, A, S>::insert(size_t index, C a)
{
  add();
  memmove(&v[index + 1], &v[index], (n - index - 1) * sizeof(C));
  v[index] = a;
}

template <class C, class A, int S>
void
Vec<C, A, S>::insert(size_t index, Vec<C> &vv)
{
  fill(n + vv.n);
  memmove(&v[index + vv.n], &v[index], (n - index - 1) * sizeof(C));
  for (int x = 0; x < vv.n; x++)
    v[index + x] = vv[x];
}

template <class C, class A, int S>
C &
Vec<C, A, S>::insert(size_t index)
{
  add();
  memmove(&v[index + 1], &v[index], (n - index - 1) * sizeof(C));
  memset(&v[index], 0, sizeof(C));
  return v[index];
}

template <class C, class A, int S>
void
Vec<C, A, S>::reverse()
{
  for (int i = 0; i < n / 2; i++) {
    C *s = &v[i], *e = &v[n - 1 - i];
    C t;
    memcpy(&t, s, sizeof(t));
    memcpy(s, e, sizeof(t));
    memcpy(e, &t, sizeof(t));
  }
}

template <class C, class A, int S>
void
Vec<C, A, S>::copy_internal(const Vec<C, A, S> &vv)
{
  int l = n, nl = (1 + VEC_INITIAL_SHIFT);
  l = l >> VEC_INITIAL_SHIFT;
  while (l) {
    l = l >> 1;
    nl++;
  }
  nl = 1 << nl;
  v  = (C *)A::alloc(nl * sizeof(C));
  memcpy(static_cast<void *>(v), vv.v, n * sizeof(C));
  memset(static_cast<void *>(v + n), 0, (nl - n) * sizeof(C));
  if (i > n) // reset reserve
    i = 0;
}

template <class C, class A, int S>
void
Vec<C, A, S>::set_expand()
{
  if (!n)
    i = SET_INITIAL_INDEX;
  else
    i = i + 1;
  n = prime2[i];
  v = (C *)A::alloc(n * sizeof(C));
  memset(static_cast<void *>(v), 0, n * sizeof(C));
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::reserve(size_t x)
{
  if (x <= n)
    return;
  unsigned xx = 1 << VEC_INITIAL_SHIFT;
  while (xx < x)
    xx *= 2;
  i        = xx;
  void *vv = (void *)v;
  v        = (C *)A::alloc(i * sizeof(C));
  if (vv && n)
    memcpy(v, vv, n * sizeof(C));
  memset(&v[n], 0, (i - n) * sizeof(C));
  if (vv && vv != e)
    A::free(vv);
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::addx()
{
  if (!v) {
    v = e;
    return;
  }
  if (v == e) {
    v = (C *)A::alloc(VEC_INITIAL_SIZE * sizeof(C));
    memcpy(static_cast<void *>(v), &e[0], n * sizeof(C));
    ink_assert(n < VEC_INITIAL_SIZE);
    memset(static_cast<void *>(&v[n]), 0, (VEC_INITIAL_SIZE - n) * sizeof(C));
  } else {
    if ((n & (n - 1)) == 0) {
      size_t nl = n * 2;
      if (nl <= i) {
        return;
      } else {
        i = 0;
      }
      void *vv = (void *)v;
      v        = (C *)A::alloc(nl * sizeof(C));
      memcpy(static_cast<void *>(v), vv, n * sizeof(C));
      memset(static_cast<void *>(&v[n]), 0, n * sizeof(C));
      A::free(vv);
    }
  }
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::reset()
{
  v = nullptr;
  n = 0;
  i = 0;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::clear()
{
  if (v && v != e)
    A::free(v);
  reset();
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::free_and_clear()
{
  for (size_t x = 0; x < (n); x++)
    A::free((void *)v[x]);
  clear();
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::delete_and_clear()
{
  for (size_t x = 0; x < n; x++) {
    if (v[x]) {
      delete v[x];
    }
  }
  clear();
}

template <class C, class A, int S> inline Vec<C, A, S>::~Vec()
{
  if (v && v != e)
    A::free(v);
}

template <class C, class A, int S>
inline int
marshal_size(Vec<C, A, S> &v)
{
  int l = sizeof(int) * 2;
  for (int x = 0; x < v.n; x++)
    l += ::marshal_size(v.v[x]);
  return l;
}

template <class C, class A, int S>
inline int
marshal(Vec<C, A, S> &v, char *buf)
{
  char *x   = buf;
  *(int *)x = v.n;
  x += sizeof(int);
  *(int *)x = v.i;
  x += sizeof(int);
  for (int i = 0; i < v.n; i++)
    x += ::marshal(v.v[i], x);
  return x - buf;
}

template <class C, class A, int S>
inline int
unmarshal(Vec<C, A, S> &v, char *buf)
{
  char *x = buf;
  v.n     = *(int *)x;
  x += sizeof(int);
  v.i = *(int *)x;
  x += sizeof(int);
  if (v.n) {
    v.v = (C *)A::alloc(sizeof(C) * v.n);
    memset(v.v, 0, sizeof(C) * v.n);
  } else
    v.v = v.e;
  for (int i = 0; i < v.n; i++)
    x += ::unmarshal(v.v[i], x);
  return x - buf;
}

template <class C, class A, int S>
inline int
Vec<C, A, S>::write(int fd)
{
  int r = 0, t = 0;
  if ((r = ::write(fd, this, sizeof(*this))) < 0)
    return r;
  t += r;
  if ((r = ::write(fd, v, n * sizeof(C))) < 0)
    return r;
  t += r;
  return t;
}

template <class C, class A, int S>
inline int
Vec<C, A, S>::read(int fd)
{
  int r = 0, t = 0;
  if ((r = ::read(fd, this, sizeof(*this))) < 0)
    return r;
  t += r;
  v = (C *)A::alloc(sizeof(C) * n);
  memset(v, 0, sizeof(C) * n);
  if ((r = ::read(fd, v, n * sizeof(C))) < 0)
    return r;
  t += r;
  return t;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::swap(C *p1, C *p2)
{
  C t = *p1;
  *p1 = *p2;
  *p2 = t;
}

template <class C>
inline void
qsort_Vec(C *left, C *right, bool (*lt)(C, C))
{
  if (right - left < 5) {
    for (C *y = right - 1; y > left; y--) {
      for (C *x = left; x < y; x++) {
        if (lt(x[1], x[0])) {
          C t  = x[0];
          x[0] = x[1];
          x[1] = t;
        }
      }
    }
  } else {
    C *center = left + ((right - left) / 2);
    C median;

    // find the median
    if (lt(*center, *left)) { // order left and center
      Vec<C>::swap(center, left);
    }
    if (lt(*(right - 1), *left)) { // order left and right
      Vec<C>::swap(right - 1, left);
    }
    if (lt(*(right - 1), *center)) { // order right and center
      Vec<C>::swap((right - 1), center);
    }
    Vec<C>::swap(center, right - 2); // stash the median one from the right for now
    median = *(right - 2);           // the median of left, center and right values

    // now partition, pivoting on the median value
    // l ptr is +1 b/c we already put the lowest of the incoming left, center
    // and right in there, ignore it for now
    // r ptr is -2 b/c we already put the biggest of the 3 values in (right-1)
    // and the median in (right -2)
    C *l = left + 1, *r = right - 2;

    // move l and r until they have something to do
    while (lt(median, *(r - 1))) {
      r--;
    }
    while (l < r && lt(*l, median)) {
      l++;
    }
    // until l and r meet,
    // compare l and median
    // swap l for r if l is larger than median
    while (l < r) {
      if (lt(*l, median)) {
        l++;
      } else {
        Vec<C>::swap(l, r - 1);
        r--;
      }
    }

    Vec<C>::swap(l, right - 2); // restore median to its rightful place

    // recurse for the littles (left segment)
    qsort_Vec<C>(left, l, lt);
    // recurse for the bigs (right segment)
    qsort_Vec<C>(l + 1, right, lt);
  }
}

template <class C>
inline void
qsort_VecRef(C *left, C *right, bool (*lt)(const C &, const C &), unsigned int *p_ctr)
{
  if (right - left < 5) {
    for (C *y = right - 1; y > left; y--) {
      for (C *x = left; x < y; x++) {
        if (lt(x[1], x[0])) {
          C t  = x[0];
          x[0] = x[1];
          x[1] = t;
        }
      }
    }
  } else {
    C *center = left + ((right - left) / 2);
    C median;

    // find the median
    if (lt(*center, *left)) { // order left and center
      Vec<C>::swap(center, left);
    }
    if (lt(*(right - 1), *left)) { // order left and right
      Vec<C>::swap(right - 1, left);
    }
    if (lt(*(right - 1), *center)) { // order right and center
      Vec<C>::swap((right - 1), center);
    }
    Vec<C>::swap(center, right - 2); // stash the median one from the right for now
    median = *(right - 2);           // the median of left, center and right values

    // now partition, pivoting on the median value
    // l ptr is +1 b/c we already put the lowest of the incoming left, center
    // and right in there, ignore it for now
    // r ptr is -2 b/c we already put the biggest of the 3 values in (right-1)
    // and the median in (right -2)
    C *l = left + 1, *r = right - 2;

    // move l and r until they have something to do
    while (lt(median, *(r - 1))) {
      r--;
    }
    while (l < r && lt(*l, median)) {
      l++;
    }
    // until l and r meet,
    // compare l and median
    // swap l for r if l is larger than median
    while (l < r) {
      if (lt(*l, median)) {
        l++;
      } else {
        Vec<C>::swap(l, r - 1);
        r--;
      }
    }

    Vec<C>::swap(l, right - 2); // restore median to its rightful place

    // recurse for the littles (left segment)
    qsort_VecRef<C>(left, l, lt, p_ctr);
    // recurse for the bigs (right segment)
    qsort_VecRef<C>(l + 1, right, lt, p_ctr);
  }
  (*p_ctr)++;
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::qsort(bool (*lt)(C, C))
{
  if (n)
    qsort_Vec<C>(&v[0], end(), lt);
}

template <class C, class A, int S>
inline void
Vec<C, A, S>::qsort(bool (*lt)(const C &, const C &))
{
  static unsigned int ctr = 0;
  if (n)
    qsort_VecRef<C>(&v[0], end(), lt, &ctr);
  Debug("qsort", "took %u iterations to sort %ld elements", ctr, n);
}
void test_vec();

typedef const char cchar;

template <class A>
static inline char *
_dupstr(cchar *s, cchar *e = nullptr)
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
  MapElem(K const &akey, C const &avalue) : key(akey), value(avalue) {}
  MapElem(MapElem const &e) : key(e.key), value(e.value) {}
  MapElem() : key(), value() {}
  MapElem &operator=(MapElem const &that) = default;
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
  void get_keys(Vec<K> &keys) const;
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
  HashMap() {}
  HashMap(C c) : invalid_value(c) {}
  MapElem<K, C> *get_internal(K akey) const;
  C get(K akey) const;
  value_type *put(K akey, C avalue);
  void get_keys(Vec<K> &keys) const;
  void get_values(Vec<C> &values);

private:
  C invalid_value = 0; // return this object if key is not present
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
Map<K, C, A>::get_keys(Vec<K> &keys) const
{
  for (size_t i = 0; i < n; i++)
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
HashMap<K, AHashFns, C, A>::get_internal(K akey) const
{
  if (!n)
    return nullptr;
  if (n <= MAP_INTEGRAL_SIZE) {
    for (MapElem<K, C> *c = v; c < v + n; c++)
      if (c->key)
        if (AHashFns::equal(akey, c->key))
          return c;
    return nullptr;
  }
  uintptr_t h = AHashFns::hash(akey);
  h           = h % n;
  for (size_t k = h, j = 0; j < i + 3; j++) {
    if (!v[k].key)
      return nullptr;
    else if (AHashFns::equal(akey, v[k].key))
      return &v[k];
    k = (k + open_hash_primes[j]) % n;
  }
  return nullptr;
}

template <class K, class AHashFns, class C, class A>
inline C
HashMap<K, AHashFns, C, A>::get(K akey) const
{
  MapElem<K, C> *x = get_internal(akey);
  if (!x)
    return invalid_value;
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
        v = e;
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
  for (size_t i = 0; i < vv.n; i++) {
    if (vv.v && vv.v[i] && vv.v[i].key) {
      put(vv.v[i].key, vv.v[i].value);
    }
  }
  return put(akey, avalue);
}

template <class K, class AHashFns, class C, class A>
inline void
HashMap<K, AHashFns, C, A>::get_keys(Vec<K> &keys) const
{
  Map<K, C, A>::get_keys(keys);
}

template <class K, class AHashFns, class C, class A>
inline void
HashMap<K, AHashFns, C, A>::get_values(Vec<C> &values)
{
  Map<K, C, A>::get_values(values);
}

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
    Location() : m_value(nullptr), m_bucket(nullptr), m_id(0), m_distance(0) {}
    /// Check for location being valid (referencing a value).
    bool
    isValid() const
    {
      return nullptr != m_value;
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
    Location &
    operator++()
    {
      if (m_value)
        this->advance();
      return *this;
    }
    /// Find next value with matching key (postfix).
    Location &
    operator++(int)
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
      @internal Iterator is @a end if @a m_value is @c nullptr.
   */
  struct iterator {
    Value *m_value;   ///< Current location.
    Bucket *m_bucket; ///< Current bucket;

    iterator() : m_value(0), m_bucket(0) {}
    iterator &operator++();
    iterator operator++(int);
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

      @note This does @b not clean up the removed elements. Use carefully to avoid leaks.

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

  // noncopyable
  TSHashTable(const TSHashTable &) = delete;
  TSHashTable &operator=(const TSHashTable &) = delete;
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
  return iterator(nullptr, nullptr);
}

template <typename H>
typename TSHashTable<H>::iterator &
TSHashTable<H>::iterator::operator++()
{
  if (m_value) {
    if (nullptr == (m_value = ListHead::next(m_value))) {        // end of bucket, next bucket.
      if (nullptr != (m_bucket = BucketChain::next(m_bucket))) { // found non-empty next bucket.
        m_value = m_bucket->m_chain.head;
        ink_assert(m_value); // if bucket is in chain, must be non-empty.
      }
    }
  }
  return *this;
}

template <typename H>
typename TSHashTable<H>::iterator
TSHashTable<H>::iterator::operator++(int)
{
  iterator prev(*this);
  ++*this;
  return prev;
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
  while (nullptr != v && !Hasher::equal(key, Hasher::key(v)))
    v = ListHead::next(v);
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
    zret = true;
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
