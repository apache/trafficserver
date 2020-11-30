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

#include "EsiGzip.h"
#include "gzip.h"
#include <cctype>
#include <cstdint>

using std::string;
using namespace EsiLib;

EsiGzip::EsiGzip(const char *debug_tag, ComponentBase::Debug debug_func, ComponentBase::Error error_func)
  : ComponentBase(debug_tag, debug_func, error_func), _downstream_length(0), _total_data_length(0), _crc(0)
{
  // Zlib _zstrm variables are initialized when they are required in runDeflateLoop
  // coverity[uninit_member]
  // coverity[uninit_ctor]
}

template <typename T>
inline void
append(string &out, T data)
{
  for (unsigned int i = 0; i < sizeof(data); ++i) {
    out += static_cast<char>(data & 0xff);
    data = data >> 8;
  }
}

inline int
runDeflateLoop(z_stream &zstrm, int flush, std::string &cdata)
{
  char buf[BUF_SIZE];
  int deflate_result = Z_OK;
  do {
    zstrm.next_out  = reinterpret_cast<Bytef *>(buf);
    zstrm.avail_out = BUF_SIZE;
    deflate_result  = deflate(&zstrm, flush);
    if ((deflate_result == Z_OK) || (deflate_result == Z_STREAM_END)) {
      cdata.append(buf, BUF_SIZE - zstrm.avail_out);
      if ((deflate_result == Z_STREAM_END) || zstrm.avail_out > 6) {
        break;
      }
    } else {
      break;
    }
  } while (true);
  return deflate_result;
}

bool
EsiGzip::stream_encode(const char *data, int data_len, std::string &cdata)
{
  const auto initial_cdata_size = cdata.size();
  if (_downstream_length == 0) {
    cdata.assign(GZIP_HEADER_SIZE, 0); // reserving space for the header
    cdata[0] = MAGIC_BYTE_1;
    cdata[1] = MAGIC_BYTE_2;
    cdata[2] = Z_DEFLATED;
    cdata[9] = OS_TYPE;

    _crc = crc32(0, Z_NULL, 0);
  }

  _zstrm.zalloc = Z_NULL;
  _zstrm.zfree  = Z_NULL;
  _zstrm.opaque = Z_NULL;
  if (deflateInit2(&_zstrm, COMPRESSION_LEVEL, Z_DEFLATED, -MAX_WBITS, ZLIB_MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
    _errorLog("[%s] deflateInit2 failed!", __FUNCTION__);
    return false;
  }

  int deflate_result = Z_OK;
  if (data && (data_len > 0)) {
    _zstrm.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    _zstrm.avail_in = data_len;
    deflate_result  = runDeflateLoop(_zstrm, Z_FULL_FLUSH, cdata);
    if (deflate_result != Z_OK) {
      _errorLog("[%s] runDeflateLoop failed!", __FUNCTION__);

      deflateEnd(&_zstrm);

      return false;
    }
    _crc = crc32(_crc, reinterpret_cast<const Bytef *>(data), data_len);
    _total_data_length += data_len;
  }
  _downstream_length += cdata.size() - initial_cdata_size;
  deflateEnd(&_zstrm);

  return true;
}

bool
EsiGzip::stream_finish(std::string &cdata, int &downstream_length)
{
  if (_downstream_length == 0) {
    // We need to run encode first to get the gzip header inserted.
    if (!stream_encode(nullptr, 0, cdata)) {
      return false;
    }
  }
  // Note that a call to stream_encode will update cdata to apply the gzip
  // header and that call itself will update _downstream_length. Since we don't
  // want to double count the gzip header bytes, we capture initial_cdata_size
  // here after any possible call to stream_encode above.
  const auto initial_cdata_size = cdata.size();
  char buf[BUF_SIZE];

  _zstrm.zalloc = Z_NULL;
  _zstrm.zfree  = Z_NULL;
  _zstrm.opaque = Z_NULL;
  if (deflateInit2(&_zstrm, COMPRESSION_LEVEL, Z_DEFLATED, -MAX_WBITS, ZLIB_MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
    _errorLog("[%s] deflateInit2 failed!", __FUNCTION__);
    return false;
  }

  _zstrm.next_in  = reinterpret_cast<Bytef *>(buf);
  _zstrm.avail_in = 0;
  // required for the "finish" loop as no data has been given so far
  int deflate_result = runDeflateLoop(_zstrm, Z_FINISH, cdata);
  deflateEnd(&_zstrm);
  if (deflate_result != Z_STREAM_END) {
    _errorLog("[%s] deflateEnd failed!", __FUNCTION__);
    downstream_length = 0;
    return false;
  }
  append(cdata, static_cast<uint32_t>(_crc));
  append(cdata, static_cast<int32_t>(_total_data_length));
  _downstream_length += cdata.size() - initial_cdata_size;
  downstream_length = _downstream_length;
  return true;
}

EsiGzip::~EsiGzip()
{
  _downstream_length = 0;
  _total_data_length = 0;
}
