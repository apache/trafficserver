/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <string_view>

class HTTPHdr;

class VersionConverter
{
public:
  int convert(HTTPHdr &header, int from, int to) const;

private:
  int _convert_nop(HTTPHdr &header) const;
  int _convert_req_from_1_to_2(HTTPHdr &header) const;
  int _convert_req_from_2_to_1(HTTPHdr &header) const;
  int _convert_res_from_1_to_2(HTTPHdr &header) const;
  int _convert_res_from_2_to_1(HTTPHdr &header) const;

  void _remove_connection_specific_header_fields(HTTPHdr &header) const;

  using convert_function = int (VersionConverter::*)(HTTPHdr &) const;

  static constexpr int MIN_VERSION = 1;
  static constexpr int MAX_VERSION = 3;
  static constexpr int N_VERSIONS  = MAX_VERSION - MIN_VERSION + 1;

  static constexpr convert_function _convert_functions[2][N_VERSIONS][N_VERSIONS] = {
    {
      // Request
      {
        // From 1
        &VersionConverter::_convert_nop,
        &VersionConverter::_convert_req_from_1_to_2,
        &VersionConverter::_convert_req_from_1_to_2,
      },
      {
        // From 2
        &VersionConverter::_convert_req_from_2_to_1,
        &VersionConverter::_convert_nop,
        &VersionConverter::_convert_nop,
      },
      {
        // From 3
        &VersionConverter::_convert_req_from_2_to_1,
        &VersionConverter::_convert_nop,
        &VersionConverter::_convert_nop,
      },
    },
    {
      // Response
      {
        // From 1
        &VersionConverter::_convert_nop,
        &VersionConverter::_convert_res_from_1_to_2,
        &VersionConverter::_convert_res_from_1_to_2,
      },
      {
        // From 2
        &VersionConverter::_convert_res_from_2_to_1,
        &VersionConverter::_convert_nop,
        &VersionConverter::_convert_nop,
      },
      {
        // From 3
        &VersionConverter::_convert_res_from_2_to_1,
        &VersionConverter::_convert_nop,
        &VersionConverter::_convert_nop,
      },
    }};

  static constexpr std::string_view connection_specific_header_fields[] = {
    "Connection", "Keep-Alive", "Proxy-Connection", "Transfer-Encoding", "Upgrade",
  };
};
