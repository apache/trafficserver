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

#include <cstdint>
#include "ts/Vec.h"

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
