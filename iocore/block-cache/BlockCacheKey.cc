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


#include "I_BlockCacheKey_util.h"

#include "P_BlockCacheKey.h"

#include "INK_MD5.h"

// implementation for BlockCacheKey utilities.  Nothing fancy

BlockCacheKey *
BlockCacheKey_util::new_from_path(const char *pathname)
{
  BlockCacheKey *k = NEW(new BlockCacheKey);
  k->init_from_path(pathname);
  return k;
}

BlockCacheKey *
BlockCacheKey_util::new_from_MD5(const INK_MD5 * md5)
{
  BlockCacheKey *k = NEW(new BlockCacheKey);
  k->init_from_md5(md5);
  return k;
}

BlockCacheKey::BlockCacheKey()
:m_path(NULL)
  , m_md5(NULL)
{
};

BlockCacheKey::~BlockCacheKey()
{
  if (m_path)
    xfree(m_path);
  if (m_md5)
    delete m_md5;
}

BlockCacheKey *
BlockCacheKey::copy() const
{
  if (m_path) {
    return BlockCacheKey_util::new_from_path(m_path);
  }
  if (m_md5) {
    return BlockCacheKey_util::new_from_MD5(m_md5);
  }
  return NULL;
}

void
BlockCacheKey::init_from_path(const char *path)
{
  m_path = ink_string_duplicate(path);
}

void
BlockCacheKey::init_from_md5(const INK_MD5 * md5)
{
  m_md5 = NEW(new INK_MD5);
  m_md5->set(md5);
}
