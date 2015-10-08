/** @file

  Multiplexes request to other origins.

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
#include <inttypes.h>

#include "dispatch.h"
#include "fetcher.h"
#include "original-request.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

extern Statistics statistics;

Request::Request(const std::string & h, const TSMBuffer b, const TSMLoc l):
  host(h), length(TSHttpHdrLengthGet(b, l)), io(new ats::io::IO()) {
  assert( ! host.empty());
  assert(b != NULL);
  assert(l != NULL);
  assert(io != NULL);
  assert(length > 0);
  TSHttpHdrPrint(b, l, io->buffer);
  assert(length == TSIOBufferReaderAvail(io->reader));
}

uint64_t copy(const TSIOBufferReader & r, const TSIOBuffer b) {
  assert(r != NULL);
  assert(b != NULL);
  TSIOBufferBlock block = TSIOBufferReaderStart(r);

  uint64_t length = 0;

  for (; block; block = TSIOBufferBlockNext(block)) {
    int64_t size = 0;
    const void * const pointer = TSIOBufferBlockReadStart(block, r, &size);

    if (pointer != NULL && size > 0) {
      const int64_t size2 = TSIOBufferWrite(b, pointer, size);
      assert(size == size2);
      length += size;
    }
  }

  return length;
}

uint64_t read(const TSIOBufferReader & r, std::string & o, int64_t l = 0) {
  assert(r != NULL);
  TSIOBufferBlock block = TSIOBufferReaderStart(r);

  assert(l >= 0);
  if (l == 0) {
    l = TSIOBufferReaderAvail(r);
    assert(l >= 0);
  }

  uint64_t length = 0;

  for (; block && l > 0; block = TSIOBufferBlockNext(block)) {
    int64_t size = 0;
    const char * const pointer = TSIOBufferBlockReadStart(block, r, &size);
    if (pointer != NULL && size > 0) {
      size = std::min(size, l);
      o.append(pointer, size);
      length += size;
      l -= size;
    }
  }

  return length;
}

uint64_t read(const TSIOBuffer & b, std::string & o, const int64_t l = 0) {
  TSIOBufferReader reader = TSIOBufferReaderAlloc(b);
  const uint64_t length = read(reader, o);
  TSIOBufferReaderFree(reader);
  return length;
}

class Handler {
  int64_t length;
  struct timespec start;
  std::string response;

public:
  const std::string url;

  Handler(std::string u) :
    length(0) {
    assert( ! u.empty());
    const_cast< std::string & >(url).swap(u);
    clock_gettime(CLOCK_MONOTONIC, &start);
  }

  void error(void) {
    TSError("[" PLUGIN_TAG "] error when communicating with \"%s\"\n", url.c_str());
    TSStatIntIncrement(statistics.failures, 1);
  }

  void timeout(void) {
    TSError("[" PLUGIN_TAG "] timeout when communicating with \"%s\"\n", url.c_str());
    TSStatIntIncrement(statistics.timeouts, 1);
  }

  void header(const TSMBuffer b, const TSMLoc l) {
    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      const TSIOBuffer buffer = TSIOBufferCreate();
      TSHttpHdrPrint(b, l, buffer);
      std::string b;
      read(buffer, b);
      TSDebug(PLUGIN_TAG, "Response header for \"%s\" was:\n%s", url.c_str(), b.c_str());
      TSIOBufferDestroy(buffer);
    }
  }

  void data(const TSIOBufferReader r, const int64_t l) {
    length += l;
    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      std::string buffer;
      const uint64_t length = read(r, buffer, l);
      response += buffer;
      TSDebug(PLUGIN_TAG, "Receiving response chunk \"%s\" of %" PRIu64 " bytes",
          buffer.c_str(), length);
    }
  }

  void done(void) {
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &end);

    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      TSDebug(PLUGIN_TAG, "Response for \"%s\" was:\n%s", url.c_str(), response.c_str());
    }

    const long diff = (end.tv_sec - start.tv_sec) * 1000000
      + (end.tv_nsec - start.tv_nsec) / 1000;

    TSStatIntIncrement(statistics.hits, 1);
    TSStatIntIncrement(statistics.time, diff);
    TSStatIntIncrement(statistics.size, length);
  }
};

void generateRequests(const Origins & o, const TSMBuffer buffer, const TSMLoc location, Requests & r) {
  assert( ! o.empty());
  assert(buffer != NULL);
  assert(location != NULL);

  Origins::const_iterator iterator = o.begin();
  const Origins::const_iterator end = o.end();

  OriginalRequest request(buffer, location);
  request.urlScheme("");
  request.urlHost("");
  request.xMultiplexerHeader("copy");

  for (; iterator != end; ++iterator) {
    const std::string & host = *iterator;
    assert( ! host.empty());
    request.hostHeader(host);
    r.push_back(Request(host, buffer, location));
  }
}

void addBody(Requests & r, const TSIOBufferReader re) {
  assert(re != NULL);
  Requests::iterator iterator = r.begin();
  const Requests::iterator end = r.end();
  const int64_t length = TSIOBufferReaderAvail(re);
  if (length == 0) {
    return;
  }
  assert(length > 0);
  for (; iterator != end; ++iterator) {
    assert(iterator->io != NULL);
    const int64_t size = copy(re, iterator->io->buffer);
    assert(size == length);
    iterator->length += size;
  }
}

void dispatch(Requests & r, const int t) {
  Requests::iterator iterator = r.begin();
  const Requests::iterator end = r.end();
  for (; iterator != end; ++iterator) {
    assert(iterator->io != NULL);
    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      TSDebug(PLUGIN_TAG, "Dispatching %i bytes to \"%s\"",
          iterator->length, iterator->host.c_str());
      std::string b;
      read(iterator->io->reader, b);
      assert(b.size() == static_cast< uint64_t >(iterator->length));
      TSDebug(PLUGIN_TAG, "%s", b.c_str());
    }
    ats::get(iterator->io, iterator->length, Handler(iterator->host), t);
    //forwarding iterator->io pointer ownership
    iterator->io = NULL;
  }
}
