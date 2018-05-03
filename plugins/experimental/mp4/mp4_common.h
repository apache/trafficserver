/*
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

#ifndef _MP4_COMMON_H
#define _MP4_COMMON_H

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <cinttypes>

#include <ts/ts.h>
#include <ts/experimental.h>
#include <ts/remap.h>
#include "mp4_meta.h"

class IOHandle
{
public:
  IOHandle() : vio(nullptr), buffer(nullptr), reader(nullptr){};

  ~IOHandle()
  {
    if (reader) {
      TSIOBufferReaderFree(reader);
      reader = nullptr;
    }

    if (buffer) {
      TSIOBufferDestroy(buffer);
      buffer = nullptr;
    }
  }

public:
  TSVIO vio;
  TSIOBuffer buffer;
  TSIOBufferReader reader;
};

class Mp4TransformContext
{
public:
  Mp4TransformContext(float offset, int64_t cl)
    : total(0), tail(0), pos(0), content_length(0), meta_length(0), parse_over(false), raw_transform(false)
  {
    res_buffer = TSIOBufferCreate();
    res_reader = TSIOBufferReaderAlloc(res_buffer);
    dup_reader = TSIOBufferReaderAlloc(res_buffer);

    mm.start = offset * 1000;
    mm.cl    = cl;
  }

  ~Mp4TransformContext()
  {
    if (res_reader) {
      TSIOBufferReaderFree(res_reader);
    }

    if (dup_reader) {
      TSIOBufferReaderFree(dup_reader);
    }

    if (res_buffer) {
      TSIOBufferDestroy(res_buffer);
    }
  }

public:
  IOHandle output;
  Mp4Meta mm;
  int64_t total;
  int64_t tail;
  int64_t pos;
  int64_t content_length;
  int64_t meta_length;

  TSIOBuffer res_buffer;
  TSIOBufferReader res_reader;
  TSIOBufferReader dup_reader;

  bool parse_over;
  bool raw_transform;
};

class Mp4Context
{
public:
  Mp4Context(float s) : start(s), cl(0), mtc(nullptr), transform_added(false){};

  ~Mp4Context()
  {
    if (mtc) {
      delete mtc;
      mtc = nullptr;
    }
  }

public:
  float start;
  int64_t cl;

  Mp4TransformContext *mtc;

  bool transform_added;
};

#endif
