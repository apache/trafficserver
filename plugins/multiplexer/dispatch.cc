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
#include <cinttypes>
#include <sys/time.h>

#include "dispatch.h"
#include "fetcher.h"
#include "original-request.h"

#ifndef PLUGIN_TAG
#error Please define a PLUGIN_TAG before including this file.
#endif

extern Statistics statistics;

size_t timeout;

Request::Request(const std::string &h, const TSMBuffer b, const TSMLoc l) : host(h), length(0), io(new ats::io::IO())
{
  assert(!host.empty());
  assert(b != nullptr);
  assert(l != nullptr);
  assert(io.get() != nullptr);
  TSHttpHdrPrint(b, l, io->buffer);
  length = TSIOBufferReaderAvail(io->reader);
  assert(length > 0);
  /*
   * TSHttpHdrLengthGet returns the size with possible "internal" headers
   * which are not printed by TSHttpHdrPrint.
   * Therefore the greater than or equal comparison
   */
  assert(TSHttpHdrLengthGet(b, l) >= length);
}

Request::Request(Request &&that) : host(std::move(that.host)), length(that.length), io(std::move(that.io))
{
  assert(!host.empty());
  assert(length > 0);
  assert(io.get() != nullptr);
}

Request &
Request::operator=(const Request &r)
{
  host   = r.host;
  length = r.length;
  io.reset(const_cast<Request &>(r).io.release());
  assert(!host.empty());
  assert(length > 0);
  assert(io.get() != nullptr);
  assert(r.io.get() == nullptr);
  return *this;
}

uint64_t
copy(const TSIOBufferReader &r, const TSIOBuffer b)
{
  assert(r != nullptr);
  assert(b != nullptr);
  TSIOBufferBlock block = TSIOBufferReaderStart(r);

  uint64_t length = 0;

  for (; block; block = TSIOBufferBlockNext(block)) {
    int64_t size              = 0;
    const void *const pointer = TSIOBufferBlockReadStart(block, r, &size);

    if (pointer != nullptr && size > 0) {
      auto const num_written = TSIOBufferWrite(b, pointer, size);
      if (num_written != size) {
        TSError("[" PLUGIN_TAG "] did not write the expected number of body bytes. "
                "Wrote: %" PRId64 ", expected: %" PRId64,
                num_written, size);
      }
      length += num_written;
    }
  }

  return length;
}

uint64_t
read(const TSIOBufferReader &r, std::string &o, int64_t l = 0)
{
  assert(r != nullptr);
  TSIOBufferBlock block = TSIOBufferReaderStart(r);

  assert(l >= 0);
  if (l == 0) {
    l = TSIOBufferReaderAvail(r);
    assert(l >= 0);
  }

  uint64_t length = 0;

  for (; block && l > 0; block = TSIOBufferBlockNext(block)) {
    int64_t size              = 0;
    const char *const pointer = TSIOBufferBlockReadStart(block, r, &size);
    if (pointer != nullptr && size > 0) {
      size = std::min(size, l);
      o.append(pointer, size);
      length += size;
      l -= size;
    }
  }

  return length;
}

uint64_t
read(const TSIOBuffer &b, std::string &o, const int64_t l = 0)
{
  TSIOBufferReader reader = TSIOBufferReaderAlloc(b);
  const uint64_t length   = read(reader, o);
  TSIOBufferReaderFree(reader);
  return length;
}

class Handler
{
  int64_t length;
  struct timeval start;
  std::string response;

public:
  const std::string url;

  Handler(std::string u) : length(0)
  {
    assert(!u.empty());
    const_cast<std::string &>(url).swap(u);
    gettimeofday(&start, nullptr);
  }

  void
  error()
  {
    TSError("[" PLUGIN_TAG "] error when communicating with \"%s\"\n", url.c_str());
    TSStatIntIncrement(statistics.failures, 1);
  }

  void
  timeout()
  {
    TSError("[" PLUGIN_TAG "] timeout when communicating with \"%s\"\n", url.c_str());
    TSStatIntIncrement(statistics.timeouts, 1);
  }

  void
  header(const TSMBuffer b, const TSMLoc l)
  {
    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      const TSIOBuffer buffer = TSIOBufferCreate();
      TSHttpHdrPrint(b, l, buffer);
      std::string buf;
      read(buffer, buf);
      TSDebug(PLUGIN_TAG, "Response header for \"%s\" was:\n%s", url.c_str(), buf.c_str());
      TSIOBufferDestroy(buffer);
    }
  }

  void
  data(const TSIOBufferReader r, const int64_t l)
  {
    length += l;
    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      std::string buffer;
      const uint64_t length = read(r, buffer, l);
      response += buffer;
      TSDebug(PLUGIN_TAG, "Receiving response chunk \"%s\" of %" PRIu64 " bytes", buffer.c_str(), length);
    }
  }

  void
  done()
  {
    struct timeval end;

    gettimeofday(&end, nullptr);

    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      TSDebug(PLUGIN_TAG, "Response for \"%s\" was:\n%s", url.c_str(), response.c_str());
    }

    const long diff = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);

    TSStatIntIncrement(statistics.hits, 1);
    TSStatIntIncrement(statistics.time, diff);
    TSStatIntIncrement(statistics.size, length);
  }
};

void
generateRequests(const Origins &o, const TSMBuffer buffer, const TSMLoc location, Requests &r)
{
  assert(!o.empty());
  assert(buffer != nullptr);
  assert(location != nullptr);

  Origins::const_iterator iterator  = o.begin();
  const Origins::const_iterator end = o.end();

  OriginalRequest request(buffer, location);
  request.urlScheme("");
  request.urlHost("");
  request.xMultiplexerHeader("copy");

  for (; iterator != end; ++iterator) {
    const std::string &host = *iterator;
    assert(!host.empty());
    request.hostHeader(host);
    r.push_back(Request(host, buffer, location));
  }
}

void
addBody(Requests &r, const TSIOBufferReader re)
{
  assert(re != nullptr);
  Requests::iterator iterator  = r.begin();
  const Requests::iterator end = r.end();
  const int64_t length         = TSIOBufferReaderAvail(re);
  if (length == 0) {
    return;
  }
  assert(length > 0);
  for (; iterator != end; ++iterator) {
    assert(iterator->io.get() != nullptr);
    const int64_t size = copy(re, iterator->io->buffer);
    assert(size == length);
    iterator->length += size;
  }
}

void
dispatch(Requests &r, const int t)
{
  Requests::iterator iterator  = r.begin();
  const Requests::iterator end = r.end();
  for (; iterator != end; ++iterator) {
    assert(iterator->io.get() != nullptr);
    if (TSIsDebugTagSet(PLUGIN_TAG) > 0) {
      TSDebug(PLUGIN_TAG, "Dispatching %i bytes to \"%s\"", iterator->length, iterator->host.c_str());
      std::string b;
      read(iterator->io->reader, b);
      assert(b.size() == static_cast<uint64_t>(iterator->length));
      TSDebug(PLUGIN_TAG, "%s", b.c_str());
    }
    // forwarding iterator->io pointer ownership
    ats::get(iterator->io.release(), iterator->length, Handler(iterator->host), t);
  }
}
