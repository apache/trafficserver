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

#include "EsiGunzip.h"
#include "gzip.h"
#include <cctype>
#include <cstdint>

using std::string;
using namespace EsiLib;

EsiGunzip::EsiGunzip(const char *debug_tag, ComponentBase::Debug debug_func, ComponentBase::Error error_func)
  : ComponentBase(debug_tag, debug_func, error_func), _downstream_length(0), _total_data_length(0)
{
  _init    = false;
  _success = true;
  // zlib _zstrm variables are initialized when they are required in stream_decode
  // coverity[uninit_member]
  // coverity[uninit_ctor]
}

bool
EsiGunzip::stream_finish()
{
  if (_init) {
    if (inflateEnd(&_zstrm) != Z_OK) {
      _errorLog("[%s] inflateEnd failed!", __FUNCTION__);
      _success = false;
    }
    _init = false;
  }

  return _success;
}

bool
EsiGunzip::stream_decode(const char *data, int data_len, std::string &udata)
{
  BufferList buf_list;

  if (!_init) {
    _zstrm.zalloc   = Z_NULL;
    _zstrm.zfree    = Z_NULL;
    _zstrm.opaque   = Z_NULL;
    _zstrm.next_in  = nullptr;
    _zstrm.avail_in = 0;

    if (inflateInit2(&_zstrm, MAX_WBITS + 16) != Z_OK) {
      _errorLog("[%s] inflateInit2 failed!", __FUNCTION__);
      _success = false;
      return false;
    }
    _init = true;
  }

  if (data && (data_len > 0)) {
    _zstrm.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    _zstrm.avail_in = data_len;
    char raw_buf[BUF_SIZE];
    int inflate_result;
    int32_t curr_buf_size;

    do {
      _zstrm.next_out  = reinterpret_cast<Bytef *>(raw_buf);
      _zstrm.avail_out = BUF_SIZE;
      inflate_result   = inflate(&_zstrm, Z_SYNC_FLUSH);
      curr_buf_size    = -1;
      if ((inflate_result == Z_OK) || (inflate_result == Z_BUF_ERROR)) {
        curr_buf_size = BUF_SIZE - _zstrm.avail_out;
      } else if (inflate_result == Z_STREAM_END) {
        curr_buf_size = BUF_SIZE - _zstrm.avail_out;
      }
      if (curr_buf_size > BUF_SIZE) {
        _errorLog("[%s] buf too large", __FUNCTION__);
        break;
      }
      if (curr_buf_size < 1) {
        _errorLog("[%s] buf below zero", __FUNCTION__);
        break;
      }

      // push empty object onto list and add data to in-list object to
      // avoid data copy for temporary
      buf_list.push_back(string());
      string &curr_buf = buf_list.back();
      curr_buf.assign(raw_buf, curr_buf_size);

      if (inflate_result == Z_STREAM_END) {
        break;
      }
    } while (_zstrm.avail_in > 0);

    _total_data_length += data_len;
  }

  for (auto &iter : buf_list) {
    udata.append(iter.data(), iter.size());
  }

  return true;
}

EsiGunzip::~EsiGunzip()
{
  _downstream_length = 0;
  _total_data_length = 0;
  _init              = false;
  _success           = true;
}
