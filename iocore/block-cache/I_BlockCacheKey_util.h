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


#ifndef _I_BlockCacheKey_util_H_
#define _I_BlockCacheKey_util_H_

class INK_MD5;

/**
  BlockCacheKey utility calls for client interface.  Exposes minimal
  implementation details.
 */
struct BlockCacheKey_util
{
  /// create BlockCacheKey from hash.
  BlockCacheKey *new_from_MD5(const INK_MD5 * hash);

    /** create BlockCacheKey from pathname.  Maybe use URL from header
      system instead when that is modularized.
      */
  BlockCacheKey *new_from_path(const char *pathname);

    /**
      create BlockCacheKey from segment id.  This is a thin layer on
      internal implementation.  e.g. in filesystem implementation, this
      would translate to a filename called 'id', and in object store
      implementation, this would translate to an array index into a vector.
      Segment id is meaningful to the application only and can be used
      to store arbitrarily large app specific data.
      */
  BlockCacheKey *new_from_segmentid(int id);
};

#endif
