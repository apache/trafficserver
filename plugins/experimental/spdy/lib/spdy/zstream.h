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

#ifndef ZSTREAM_H_EA418AC6_C57B_4597_9748_7C11D04B6586
#define ZSTREAM_H_EA418AC6_C57B_4597_9748_7C11D04B6586

#include <inttypes.h>
#include <zlib.h>
#include <string.h>

namespace spdy
{
enum zstream_error {
  z_ok = 0,
  z_stream_end,
  z_need_dict,
  z_errno,
  z_stream_error,
  z_data_error,
  z_memory_error,
  z_buffer_error,
  z_version_error
};

template <typename ZlibMechanism> struct zstream : public ZlibMechanism {
  zstream()
  {
    memset(&stream, 0, sizeof(stream));
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    ZlibMechanism::init(&stream);
  }

  bool
  drained() const
  {
    return stream.avail_in == 0;
  }

  template <typename T, typename N>
  void
  input(T *ptr, N nbytes)
  {
    stream.next_in = (uint8_t *)ptr;
    stream.avail_in = nbytes;
  }

  // Consume the input without producing any output.
  zstream_error
  consume()
  {
    zstream_error ret;
    stream.next_out = (uint8_t *)1;
    stream.avail_out = 0;
    ret = ZlibMechanism::transact(&stream, 0);
    return (ret == z_buffer_error) ? z_ok : ret;
  }

  // Return the number of output bytes or negative zstream_error on failure.
  template <typename T, typename N>
  ssize_t
  consume(T *ptr, N nbytes, unsigned flags = Z_SYNC_FLUSH)
  {
    zstream_error ret;
    stream.next_out = (uint8_t *)ptr;
    stream.avail_out = nbytes;

    ret = ZlibMechanism::transact(&stream, flags);
    if (ret == z_buffer_error) {
      return 0;
    }

    if (ret == z_ok || ret == z_stream_end) {
      // return the number of bytes produced
      return nbytes - stream.avail_out;
    }

    return -ret;
  }

  ~zstream() { ZlibMechanism::destroy(&stream); }

private:
  zstream(const zstream &);            // disable
  zstream &operator=(const zstream &); // disable

  z_stream stream;
};

struct decompress {
  zstream_error init(z_stream *zstr);
  zstream_error transact(z_stream *zstr, int flush);
  zstream_error destroy(z_stream *zstr);
};

struct compress {
  zstream_error init(z_stream *zstr);
  zstream_error transact(z_stream *zstr, int flush);
  zstream_error destroy(z_stream *zstr);
};

} // namespace spdy

#endif /* ZSTREAM_H_EA418AC6_C57B_4597_9748_7C11D04B6586 */
/* vim: set sw=4 ts=4 tw=79 et : */
