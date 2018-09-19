/* -*-Mode: c++;-*-
  Various vector related code.

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

/* UnionFind after Tarjan */

#include "catch.hpp"

#include <cstdint>
#include <cstdio>
#include "tscore/ink_assert.h"
#include "tscore/Map.h"

// Intervals store sets in interval format (e.g. [1..10][12..12]).
// Inclusion test is by binary search on intervals.
// Deletion is not supported
class Intervals : public Vec<int>
{
public:
  void insert(int n);
  bool in(int n) const;
};

// UnionFind supports fast unify and finding of
// 'representitive elements'.
// Elements are numbered from 0 to N-1.
class UnionFind : public Vec<int>
{
public:
  // set number of elements, initialized to singletons, may be called repeatedly to increase size
  void size(int n);
  // return representitive element
  int find(int n);
  // unify the sets containing the two elements
  void unify(int n, int m);
};

// binary search over intervals
static int
i_find(const Intervals *i, int x)
{
  ink_assert(i->n);
  int l = 0, h = i->n;
Lrecurse:
  if (h <= l + 2) {
    if (h <= l) {
      return -(l + 1);
    }
    if (x < i->v[l] || x > i->v[l + 1]) {
      return -(l + 1);
    }
    return h;
  }
  int m = (((h - l) / 4) * 2) + l;
  if (x > i->v[m + 1]) {
    l = m;
    goto Lrecurse;
  }
  if (x < i->v[m]) {
    h = m;
    goto Lrecurse;
  }
  return (l + 1);
}

bool
Intervals::in(int x) const
{
  if (!n) {
    return false;
  }
  if (i_find(this, x) > 0) {
    return true;
  }
  return false;
}

// insert into interval with merge
void
Intervals::insert(int x)
{
  if (!n) {
    add(x);
    add(x);
    return;
  }
  int l = i_find(this, x);
  if (l > 0) {
    return;
  }
  l = -l - 1;

  if (x > v[l + 1]) {
    if (x == v[l + 1] + 1) {
      v[l + 1]++;
      goto Lmerge;
    }
    l += 2;
    if (l < (int)n) {
      if (x == v[l] - 1) {
        v[l]--;
        goto Lmerge;
      }
    }
    goto Lmore;
  } else {
    ink_assert(x < v[l]);
    if (x == v[l] - 1) {
      v[l]--;
      goto Lmerge;
    }
    if (!l) {
      goto Lmore;
    }
    l -= 2;
    if (x == v[l + 1] + 1) {
      v[l + 1]++;
      goto Lmerge;
    }
  }
Lmore:
  fill(n + 2);
  if (n - 2 - l > 0) {
    memmove(v + l + 2, v + l, sizeof(int) * (n - 2 - l));
  }
  v[l]     = x;
  v[l + 1] = x;
  return;
Lmerge:
  if (l) {
    if (v[l] - v[l - 1] < 2) {
      l -= 2;
      goto Ldomerge;
    }
  }
  if (l < (int)(n - 2)) {
    if (v[l + 2] - v[l + 1] < 2) {
      goto Ldomerge;
    }
  }
  return;
Ldomerge:
  memmove(v + l + 1, v + l + 3, sizeof(int) * (n - 3 - l));
  n -= 2;
  goto Lmerge;
}

void
UnionFind::size(int s)
{
  size_t nn = n;
  fill(s);
  for (size_t i = nn; i < n; i++) {
    v[i] = -1;
  }
}

int
UnionFind::find(int n)
{
  int i, t;
  for (i = n; v[i] >= 0; i = v[i]) {
    ;
  }
  while (v[n] >= 0) {
    t    = n;
    n    = v[n];
    v[t] = i;
  }
  return i;
}

void
UnionFind::unify(int n, int m)
{
  n = find(n);
  m = find(m);
  if (n != m) {
    if (v[m] < v[n]) {
      v[m] += (v[n] - 1);
      v[n] = m;
    } else {
      v[n] += (v[m] - 1);
      v[m] = n;
    }
  }
}

TEST_CASE("test append", "[Vec]")
{
  static const char value[] = "this is a string";
  unsigned int len          = (int)sizeof(value) - 1;

  Vec<char> str;

  str.append(value, 0);
  REQUIRE(str.length() == 0);

  str.append(value, len);
  REQUIRE(memcmp(&str[0], value, len) == 0);
  REQUIRE(str.length() == len);

  str.clear();
  REQUIRE(str.length() == 0);

  for (unsigned i = 0; i < 1000; ++i) {
    str.append(value, len);
    REQUIRE(memcmp(&str[i * len], value, len) == 0);
  }

  REQUIRE(str.length() == 1000 * len);
}

TEST_CASE("test basic", "[libts][Vec]")
{
  Vec<void *> v, vv, vvv;
  int tt = 99 * 50, t = 0;

  for (size_t i = 0; i < 100; i++) {
    v.add((void *)(intptr_t)i);
  }
  for (size_t i = 0; i < 100; i++) {
    t += (int)(intptr_t)v.v[i];
  }
  REQUIRE(t == tt);

  t = 0;
  for (size_t i = 1; i < 100; i++) {
    vv.set_add((void *)(intptr_t)i);
  }
  for (size_t i = 1; i < 100; i++) {
    vvv.set_add((void *)(intptr_t)i);
  }
  for (size_t i = 1; i < 100; i++) {
    vvv.set_add((void *)(intptr_t)(i * 1000));
  }
  vv.set_union(vvv);
  for (size_t i = 0; i < vv.n; i++) {
    if (vv.v[i]) {
      t += (int)(intptr_t)vv.v[i];
    }
  }
  REQUIRE(t == tt + 1000 * tt);

  v.clear();
  v.reserve(1000);
  t = 0;
  for (size_t i = 0; i < 1000; i++) {
    v.add((void *)(intptr_t)i);
  }
  for (size_t i = 0; i < 1000; i++) {
    t += (int)(intptr_t)v.v[i];
  }
  REQUIRE(t == 999 * 500);
  printf("%zu %zu\n", v.n, v.i);

  Intervals in;
  in.insert(1);
  REQUIRE(in.n == 2);
  in.insert(2);
  REQUIRE(in.n == 2);
  in.insert(6);
  REQUIRE(in.n == 4);
  in.insert(7);
  REQUIRE(in.n == 4);
  in.insert(9);
  REQUIRE(in.n == 6);
  in.insert(4);
  REQUIRE(in.n == 8);
  in.insert(5);
  REQUIRE(in.n == 6);
  in.insert(3);
  REQUIRE(in.n == 4);
  in.insert(8);
  REQUIRE(in.n == 2);

  UnionFind uf;
  uf.size(4);
  uf.unify(0, 1);
  uf.unify(2, 3);
  REQUIRE(uf.find(2) == uf.find(3));
  REQUIRE(uf.find(0) == uf.find(1));
  REQUIRE(uf.find(0) != uf.find(3));
  REQUIRE(uf.find(1) != uf.find(3));
  REQUIRE(uf.find(1) != uf.find(2));
  REQUIRE(uf.find(0) != uf.find(2));
  uf.unify(1, 2);
  REQUIRE(uf.find(0) == uf.find(3));
  REQUIRE(uf.find(1) == uf.find(3));
}

static bool
compare(void *a, void *b)
{
  return a < b;
}

TEST_CASE("test sort", "[libts][Vec]")
{
  Vec<void *> v;
  for (long i = 1; i <= 1000; ++i) {
    v.add(reinterpret_cast<void *>(static_cast<intptr_t>(((i * 149) % 1000) + 1)));
  }
  v.qsort(&compare);
  for (int i = 0; i < 1000; ++i) {
    REQUIRE(reinterpret_cast<void *>(static_cast<intptr_t>(i + 1)) == v[i]);
  }

  v.clear();
  for (long i = 1; i <= 1000000; ++i) {
    v.add(reinterpret_cast<void *>(static_cast<intptr_t>(((i * 51511) % 1000000) + 1)));
  }
  v.qsort(&compare);

  for (long i = 0; i < 1000000; ++i) {
    REQUIRE(reinterpret_cast<void *>(static_cast<intptr_t>(i + 1)) == v[i]);
  }

  v.clear();
  for (long i = 1; i <= 1000000; ++i) {
    // This should be every number 1..500000 twice.
    v.add(reinterpret_cast<void *>(static_cast<intptr_t>(((i * 199999) % 500000) + 1)));
  }
  v.qsort(&compare);

  for (long i = 0; i < 1000000; ++i) {
    REQUIRE(reinterpret_cast<void *>(static_cast<intptr_t>((i / 2) + 1)) == v[i]);
  }

  // Very long array, already sorted. This is what broke before.
  v.clear();
  for (long i = 1; i <= 10000000; ++i) {
    v.add(reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
  v.qsort(&compare);
  for (long i = 0; i < 10000000; ++i) {
    REQUIRE(reinterpret_cast<void *>(static_cast<intptr_t>(i + 1)) == v[i]);
  }

  // very long, reverse sorted.
  v.clear();
  for (long i = 10000000; i >= 1; --i) {
    v.add(reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }
  v.qsort(&compare);
  for (long i = 0; i < 10000000; ++i) {
    REQUIRE(reinterpret_cast<void *>(static_cast<intptr_t>(i + 1)) == v[i]);
  }
}
