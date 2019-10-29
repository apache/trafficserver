/** @file

   In debug loads, provides functions that can be added to catch memory block duplicate frees or use of
   stale pointers.

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

#ifdef DEBUG

#include <cstddef>

namespace ts
{
class MemBlkLife
{
public:
  MemBlkLife();

  ~MemBlkLife();

  // Indicate start of life of given memory block.
  //
  void start(void const *base_addr, std::size_t size);

  // Indicate end of life of given memory block.  To be thread-safe, it's important that this is done BEFORE the
  // memory block is freed and available for reallocation.
  //
  void end(void const *base_addr);

  // Returns true if the address is within a block whose life was started and has not yet been ended.
  //
  bool isAlive(void const *addr);

  // Get life ID of address in a block known to be alive.
  //
  unsigned getLifeId(void const *addr);

  // Cause program to abort if address is not within a live block with the given life ID.
  //
  void checkLifeId(void const *addr, unsigned life_id);

}; // end class MemBlkLife

} // end namespace ts

#define MEM_BLK_LIFE_ID_DEFINE(NAME) unsigned NAME;

#define MEM_BLK_LIFE_ID_SET(ID, ADDR) ((ID) = ts::MemBlkLife().getLifeId(ADDR))

#define MEM_BLK_LIFE_ID_CHECK(ADDR, ID) ts::MemBlkLife().checkLifeId((ADDR), (ID))

#else

namespace ts
{
class MemBlkLife
{
public:
  void
  start(void const *, std::size_t)
  {
  }

  void
  end(void const *)
  {
  }

  bool
  isAlive(void const *)
  {
    return true;
  }

  unsigned
  getLifeId(void const *)
  {
    return 0;
  }

  void
  checkLifeId(void const *, unsigned)
  {
  }

}; // end class MemBlkLife

} // end namespace ts

#define MEM_BLK_LIFE_ID_DEFINE(NAME)

#define MEM_BLK_LIFE_ID_SET(ID, ADDR)

#define MEM_BLK_LIFE_ID_CHECK(ADDR, ID)

#endif // !defined(DEBUG)
