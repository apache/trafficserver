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

#include <ts/ts.h>
#include <netinet/in.h>
#include <memory>

#define AuthLogDebug(fmt, ...) TSDebug("authproxy", "%s: " fmt, __func__, ##__VA_ARGS__)
#define AuthLogError(fmt, ...) TSError(fmt, ##__VA_ARGS__)

template <typename T>
T *
AuthNew()
{
  return new (TSmalloc(sizeof(T))) T();
}

template <typename T>
void
AuthDelete(T *ptr)
{
  ptr->~T();
  TSfree(ptr);
}

struct HttpIoBuffer {
  TSIOBuffer buffer;
  TSIOBufferReader reader;

  explicit HttpIoBuffer(TSIOBufferSizeIndex size = TS_IOBUFFER_SIZE_INDEX_32K)
    : buffer(TSIOBufferSizedCreate(size)), reader(TSIOBufferReaderAlloc(buffer))
  {
  }

  ~HttpIoBuffer()
  {
    TSIOBufferReaderFree(this->reader);
    TSIOBufferDestroy(this->buffer);
  }

  void
  reset(TSIOBufferSizeIndex size = TS_IOBUFFER_SIZE_INDEX_32K)
  {
    TSIOBufferReaderFree(this->reader);
    TSIOBufferDestroy(this->buffer);
    this->buffer = TSIOBufferSizedCreate(size);
    this->reader = TSIOBufferReaderAlloc(this->buffer);
  }

  void
  consume(size_t nbytes)
  {
    TSIOBufferReaderConsume(this->reader, nbytes);
  }

  // noncopyable
  HttpIoBuffer(const HttpIoBuffer &) = delete;            // delete
  HttpIoBuffer &operator=(const HttpIoBuffer &) = delete; // delete
};

struct HttpHeader {
  HttpHeader() : buffer(TSMBufferCreate()), header(TSHttpHdrCreate(buffer)) {}
  ~HttpHeader()
  {
    TSHttpHdrDestroy(this->buffer, this->header);

    TSHandleMLocRelease(this->buffer, TS_NULL_MLOC, this->header);
    TSMBufferDestroy(this->buffer);
  }

  TSMBuffer buffer;
  TSMLoc header;

  // noncopyable
  HttpHeader(const HttpHeader &) = delete;            // delete
  HttpHeader &operator=(const HttpHeader &) = delete; // delete
};

// Return true if the given HTTP header specified chunked transfer encoding.
bool HttpIsChunkedEncoding(TSMBuffer mbuf, TSMLoc mhdr);

// Return the value of the Content-Length header.
unsigned HttpGetContentLength(TSMBuffer mbuf, TSMLoc mhdr);

// Set the value of an arbitrary HTTP header.
void HttpSetMimeHeader(TSMBuffer mbuf, TSMLoc mhdr, const char *name, const char *value);
void HttpSetMimeHeader(TSMBuffer mbuf, TSMLoc mhdr, const char *name, unsigned value);

// Dump the given HTTP header to the debug log.
void HttpDebugHeader(TSMBuffer mbuf, TSMLoc mhdr);

// vim: set ts=4 sw=4 et :
