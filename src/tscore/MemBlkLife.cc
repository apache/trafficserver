/** @file

    Implemention file fore MemBlkLife.h.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more
    contributor license agreements.  See the NOTICE file distributed with this
    work for additional information regarding copyright ownership.  The ASF
    licenses this file to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
    License for the specific language governing permissions and limitations under
    the License.
*/

#ifdef DEBUG

#include <tscore/MemBlkLife.h>

#include <map>
#include <mutex>

#include <tscore/ink_assert.h>

namespace
{
bool static_destruction_happened_;

struct Dummy {
  ~Dummy();
};

Dummy dummy;

// Mutex for access for data in anonymous namespace.  Has to be defined after dummy so it's destroyed after dummy.
//
std::mutex mtx;

Dummy::~Dummy()
{
  std::lock_guard<std::mutex> lg(mtx);

  static_destruction_happened_ = true;
}

// This should be called after mtx is locked.
//
void
check_static_destruction()
{
  ink_assert(!static_destruction_happened_);
}

unsigned life_id_counter;

struct BlockData {
  std::size_t const size;
  unsigned const life_id;

  BlockData(std::size_t size_, unsigned id) : size(size_), life_id(id) {}
};

std::map<char const *, BlockData> bmap;

std::map<char const *, BlockData>::iterator
findBlockLessThanOrEqualTo(char const *addr)
{
  if (bmap.empty()) {
    return bmap.end();
  }
  auto it = bmap.upper_bound(addr);

  if (bmap.end() == it) {
    return bmap.rbegin().base();
  }
  return --it;
}

} // end anonymous namespace

namespace ts
{
MemBlkLife::MemBlkLife()
{
  mtx.lock();
}

MemBlkLife::~MemBlkLife()
{
  mtx.unlock();
}

void
MemBlkLife::start(void const *base_addr_, std::size_t size)
{
  check_static_destruction();

  auto base_addr = static_cast<char const *>(base_addr_);

  if (!bmap.empty()) {
    // Make sure starting block does not overlap any living block.

    auto it = bmap.lower_bound(base_addr);

    if (bmap.end() == it) {
      // base_addr is greater than the greatest key in the map.
      //
      it = bmap.rbegin().base();

    } else {
      ink_assert((base_addr + size) <= it->first);

      if (bmap.begin() == it) {
        // base_addr is less than the least key in the map.
        //
        it = bmap.end();

      } else {
        --it;
      }
    }
    if (it != bmap.end()) {
      // it at this point is pointing to the largest key in the map strictly less than base_addr.

      ink_assert(base_addr >= (it->first + it->second.size));
    }
  }
  bmap.emplace(base_addr, BlockData(size, life_id_counter++));
}

void
MemBlkLife::end(void const *base_addr)
{
  check_static_destruction();

  ink_assert(bmap.erase(static_cast<char const *>(base_addr)) == 1);
}

bool
MemBlkLife::isAlive(void const *addr_)
{
  check_static_destruction();

  auto addr = static_cast<char const *>(addr_);

  auto it = findBlockLessThanOrEqualTo(addr);

  if (it != bmap.end()) {
    return addr < (it->first + it->second.size);
  }
  return false;
}

unsigned
MemBlkLife::getLifeId(void const *addr_)
{
  check_static_destruction();

  auto addr = static_cast<char const *>(addr_);

  auto it = findBlockLessThanOrEqualTo(addr);

  // Has to be alive.
  //
  ink_assert((it != bmap.end()) && (addr < (it->first + it->second.size)));

  return it->second.life_id;
}

void
MemBlkLife::checkLifeId(void const *addr, unsigned life_id)
{
  ink_assert(getLifeId(addr) == life_id);
}

} // end namespace ts

#endif // defined(DEBUG)
