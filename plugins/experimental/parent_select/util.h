/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

// ScopedFreeMLoc frees the given TSMLoc with TSHandleMLocRelease(buf, parent, *m) when it goes out of scope.
// The lifetime of buf and parent must exceed this.
// The parent must be allocated before this (if it exists).
// The parent may be TS_NULL_MLOC.
// If mloc is set to TS_NULL_MLOC or never allocated, it will not be freed.
struct ScopedFreeMLoc {
  ScopedFreeMLoc(TSMBuffer *_buf, TSMLoc _parent, TSMLoc *_mloc) : mloc(_mloc), parent(_parent), buf(_buf){};
  ~ScopedFreeMLoc()
  {
    if (*mloc != TS_NULL_MLOC) {
      TSHandleMLocRelease(*buf, parent, *mloc);
    }
  };

private:
  TSMLoc *mloc;
  TSMLoc parent;
  TSMBuffer *buf;
};

// StrVal is a string as returned by TSUrlStringGet and other TS API functions.
// Zeroes on initialization.
struct StrVal {
  StrVal() : ptr(nullptr), len(0){};
  char *ptr;
  int len;
};

// ScopedFreeStrVal frees the ptr in the given Strval when it goes out of scope.
struct ScopedFreeStrVal {
  ScopedFreeStrVal(StrVal *_strval) : strval(_strval){};
  ~ScopedFreeStrVal() { TSfree(strval->ptr); };

private:
  StrVal *strval;
};
