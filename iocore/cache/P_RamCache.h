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

#ifndef _P_RAM_CACHE_H__
#define _P_RAM_CACHE_H__

#include "I_Cache.h"

// Generic Ram Cache interface

struct RamCache {
  // returns 1 on found/stored, 0 on not found/stored, if provided auxkey1 and auxkey2 must match
  virtual int get(INK_MD5 *key, Ptr<IOBufferData> *ret_data, inku32 auxkey1 = 0, inku32 auxkey2 = 0) = 0;
  virtual int put(INK_MD5 *key, IOBufferData *data, inku32 len, bool copy = false, inku32 auxkey1 = 0, inku32 auxkey2 = 0) = 0;
  virtual int fixup(INK_MD5 *key, inku32 old_auxkey1, inku32 old_auxkey2, inku32 new_auxkey1, inku32 new_auxkey2) = 0;

  virtual void init(ink64 max_bytes, Part *part) = 0;
};

RamCache *new_RamCacheLRU();
RamCache *new_RamCacheCLFUS();

#endif /* _P_RAM_CACHE_H__ */
