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

#pragma once

#include "ComponentBase.h"
#include <zlib.h>
#include <string>
#include <string_view>

class EsiGzip : private EsiLib::ComponentBase
{
public:
  EsiGzip(const char *debug_tag, EsiLib::ComponentBase::Debug debug_func, EsiLib::ComponentBase::Error error_func);

  ~EsiGzip() override;

  /** Compress the provided content.
   *
   * @param[in] data The input data to compress.
   * @param[in] data_len The length of the input data to compress.
   * @param[in,out] The result of compressing the input data will be appended
   *    to cdata.
   *
   * @return True if the compression succeeded, false otherwise.
   */
  bool stream_encode(const char *data, int data_len, std::string &cdata);

  /** A string_view overload of stream_encode. */
  inline bool
  stream_encode(std::string_view data, std::string &cdata)
  {
    return stream_encode(data.data(), data.size(), cdata);
  }

  /** Finish the compression stream.
   *
   * @param[out] cdata The compressed data is appended to this.
   * @param[out] downstream_length The total number of compressed stream bytes
   *    across all calls to stream_encode and stream_finish.
   *
   * @return True if the compression succeeded, false otherwise.
   */
  bool stream_finish(std::string &cdata, int &downstream_length);

private:
  /** The cumulative total number of bytes for the compressed stream. */
  int _downstream_length;

  /** The cumulative total number of uncompressed bytes that have been
   * compressed.
   */
  int _total_data_length;
  z_stream _zstrm;
  uLong _crc;
};
