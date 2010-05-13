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


#ifndef _P_BlockCacheKey_H_
#define _P_BlockCacheKey_H_

class INK_MD5;
/**
   This is intended to be used by cache internals only.  External
   callers should use BlockCacheKey_util to create keys.
*/

class BlockCacheKey
{
public:
  /// minimal constructor -- init_from_* does most of the work
  BlockCacheKey();
  /**
     initialize from path
     @param path must be valid pathname -- not checked right now
  */
  void init_from_path(const char *path);
  /**
     initialize from md5
     @param md5
  */
  void init_from_md5(const INK_MD5 * md5);

  // some accessors to get path (converting md5 to path), or md5
  // (converting path to md5)

  /// return new copy of the BlockCacheKey object
  BlockCacheKey *copy() const;

private:
  char *m_path;
  /**
     Note the positive implications of using INK_MD5 * instead of
     INK_MD5 directly.
     The implementation (or anyone who includes this header file) is
     now not forced to be coupled to the libinktomi library headers
     (and transitively potentially coupled to any headers that it is
     dependent on...).
  */
  INK_MD5 *m_md5;
};

#endif
