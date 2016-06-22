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

#include <stdint.h>

#include "gzip.h"
#include <zlib.h>

#include "Utils.h"

using namespace EsiLib;
using std::string;

template <typename T>
inline void
append(string &out, T data)
{
  for (unsigned int i = 0; i < sizeof(data); ++i) {
    out += static_cast<char>(data & 0xff);
    data = data >> 8;
  }
}

template <typename T>
inline void
extract(const char *in, T &data)
{
  data = 0;
  for (int i = (sizeof(data) - 1); i >= 0; --i) {
    data = data << 8;
    data = data | static_cast<unsigned char>(*(in + i));
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
      if ((deflate_result == Z_STREAM_END) || zstrm.avail_out) {
        break;
      }
    } else {
      break;
    }
  } while (true);
  return deflate_result;
}

bool
EsiLib::gzip(const ByteBlockList &blocks, std::string &cdata)
{
  cdata.assign(GZIP_HEADER_SIZE, 0); // reserving space for the header
  z_stream zstrm;
  zstrm.zalloc = Z_NULL;
  zstrm.zfree  = Z_NULL;
  zstrm.opaque = Z_NULL;
  if (deflateInit2(&zstrm, COMPRESSION_LEVEL, Z_DEFLATED, -MAX_WBITS, ZLIB_MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
    Utils::ERROR_LOG("[%s] deflateInit2 failed!", __FUNCTION__);
    return false;
  }

  int total_data_len = 0;
  uLong crc          = crc32(0, Z_NULL, 0);
  int deflate_result = Z_OK;
  int in_data_size   = 0;
  for (ByteBlockList::const_iterator iter = blocks.begin(); iter != blocks.end(); ++iter) {
    if (iter->data && (iter->data_len > 0)) {
      zstrm.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(iter->data));
      zstrm.avail_in = iter->data_len;
      in_data_size += iter->data_len;
      deflate_result = runDeflateLoop(zstrm, 0, cdata);
      if (deflate_result != Z_OK) {
        break; // break out of the blocks iteration
      }
      crc = crc32(crc, reinterpret_cast<const Bytef *>(iter->data), iter->data_len);
      total_data_len += iter->data_len;
    }
  }
  if (!in_data_size) {
    zstrm.avail_in = 0; // required for the "finish" loop as no data has been given so far
  }
  if (deflate_result == Z_OK) {
    deflate_result = runDeflateLoop(zstrm, Z_FINISH, cdata);
  }
  deflateEnd(&zstrm);
  if (deflate_result != Z_STREAM_END) {
    Utils::ERROR_LOG("[%s] Failure while deflating; error code %d", __FUNCTION__, deflate_result);
    return false;
  }
  cdata[0] = MAGIC_BYTE_1;
  cdata[1] = MAGIC_BYTE_2;
  cdata[2] = Z_DEFLATED;
  cdata[9] = OS_TYPE;
  append(cdata, static_cast<uint32_t>(crc));
  append(cdata, static_cast<int32_t>(total_data_len));
  return true;
}

bool
EsiLib::gunzip(const char *data, int data_len, BufferList &buf_list)
{
  if (!data || (data_len <= (GZIP_HEADER_SIZE + GZIP_TRAILER_SIZE))) {
    Utils::ERROR_LOG("[%s] Invalid arguments: 0x%p, %d", __FUNCTION__, data, data_len);
    return false;
  }
  if ((data[0] != MAGIC_BYTE_1) || (data[1] != MAGIC_BYTE_2) || (data[2] != Z_DEFLATED) || (data[9] != OS_TYPE)) {
    Utils::ERROR_LOG("[%s] Header check failed!", __FUNCTION__);
    return false;
  }
  data += GZIP_HEADER_SIZE;
  data_len -= (GZIP_HEADER_SIZE + GZIP_TRAILER_SIZE);
  buf_list.clear();
  z_stream zstrm;
  zstrm.zalloc   = Z_NULL;
  zstrm.zfree    = Z_NULL;
  zstrm.opaque   = Z_NULL;
  zstrm.next_in  = 0;
  zstrm.avail_in = 0;
  if (inflateInit2(&zstrm, -MAX_WBITS) != Z_OK) {
    Utils::ERROR_LOG("[%s] inflateInit2 failed!", __FUNCTION__);
    return false;
  }
  zstrm.next_in  = reinterpret_cast<Bytef *>(const_cast<char *>(data));
  zstrm.avail_in = data_len;
  char raw_buf[BUF_SIZE];
  int inflate_result;
  int32_t unzipped_data_size = 0;
  int32_t curr_buf_size;
  uLong crc = crc32(0, Z_NULL, 0);
  do {
    zstrm.next_out  = reinterpret_cast<Bytef *>(raw_buf);
    zstrm.avail_out = BUF_SIZE;
    inflate_result  = inflate(&zstrm, Z_SYNC_FLUSH);
    curr_buf_size   = -1;
    if ((inflate_result == Z_OK) || (inflate_result == Z_BUF_ERROR)) {
      curr_buf_size = BUF_SIZE;
    } else if (inflate_result == Z_STREAM_END) {
      curr_buf_size = BUF_SIZE - zstrm.avail_out;
    }
    if (curr_buf_size > BUF_SIZE) {
      Utils::ERROR_LOG("[%s] buf too large", __FUNCTION__);
      break;
    }
    if (curr_buf_size < 1) {
      Utils::ERROR_LOG("[%s] buf below zero", __FUNCTION__);
      break;
    }
    unzipped_data_size += curr_buf_size;
    crc = crc32(crc, reinterpret_cast<const Bytef *>(raw_buf), curr_buf_size);

    // push empty object onto list and add data to in-list object to
    // avoid data copy for temporary
    buf_list.push_back(string());
    string &curr_buf = buf_list.back();
    curr_buf.assign(raw_buf, curr_buf_size);

    if (inflate_result == Z_STREAM_END) {
      break;
    }
  } while (zstrm.avail_in > 0);
  inflateEnd(&zstrm);
  if (inflate_result != Z_STREAM_END) {
    Utils::ERROR_LOG("[%s] Failure while inflating; error code %d", __FUNCTION__, inflate_result);
    return false;
  }
  int32_t orig_size;
  uint32_t orig_crc;
  extract(data + data_len, orig_crc);
  extract(data + data_len + 4, orig_size);
  if ((crc != orig_crc) || (unzipped_data_size != orig_size)) {
    Utils::ERROR_LOG("[%s] CRC/size error. Expecting (CRC, size) (0x%x, 0x%x); computed (0x%x, 0x%x)", __FUNCTION__, orig_crc,
                     orig_size, crc, unzipped_data_size);
    return false;
  }
  return true;
}
