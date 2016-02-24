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
#include <spdy/spdy.h>
#include "io.h"
#include "ink_memory.h"

spdy_io_control::spdy_io_control(TSVConn v) : vconn(v), input(), output(), streams(), last_stream_id(0)
{
}

spdy_io_control::~spdy_io_control()
{
  TSVConnClose(vconn);

  for (auto ptr(streams.begin()); ptr != streams.end(); ++ptr) {
    release(ptr->second);
  }
}

void
spdy_io_control::reenable()
{
  TSVIO vio = TSVConnWriteVIOGet(this->vconn);
  TSMutex mutex = TSVIOMutexGet(vio);

  TSMutexLock(mutex);
  TSVIOReenable(vio);
  TSMutexUnlock(mutex);
}

bool
spdy_io_control::valid_client_stream_id(unsigned stream_id) const
{
  if (stream_id == 0) {
    return false;
  } // must not be zero
  if ((stream_id % 2) == 0) {
    return false;
  } // must be odd
  return stream_id > last_stream_id;
}

spdy_io_stream *
spdy_io_control::create_stream(unsigned stream_id)
{
  ats_scoped_obj<spdy_io_stream> ptr(new spdy_io_stream(stream_id));
  std::pair<stream_map_type::iterator, bool> result;

  result = streams.insert(std::make_pair(stream_id, ptr.get()));
  if (result.second) {
    // Insert succeeded, hold a refcount on the stream.
    retain(ptr.get());
    last_stream_id = stream_id;
    return ptr.release();
  }

  // stream-id collision ... fail and autorelease.
  return NULL;
}

void
spdy_io_control::destroy_stream(unsigned stream_id)
{
  stream_map_type::iterator ptr(streams.find(stream_id));
  if (ptr != streams.end()) {
    std::lock_guard<spdy_io_stream::lock_type> lk(ptr->second->lock);
    release(ptr->second);
    streams.erase(ptr);
  }
}

/* vim: set sw=4 ts=4 tw=79 et : */
