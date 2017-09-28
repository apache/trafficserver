/**
  @file
  @brief The prefix parser interface and error type are located here.

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

#ifndef __PREFIX_PARSER_H__
#define __PREFIX_PARSER_H__ 1

#include <ts/ts.h>
#include <atscppapi/Logger.h>

#include "ts/ink_inet.h"
#include "ts/IpMap.h"

#include "default.h"

enum class PrefixParseError { ok, bad_prefix, bad_ip };

/**
  Converts a range of IPs (both v4 and v6) from a CIDR-encoded string to two sockaddr datastructures representing the lower/upper
  bounds of the IP range.
  Returns PrefixParseError::ok only on successful conversion.
*/
extern PrefixParseError parse_addresses(const char *prefixedAddress, int prefix_num, sockaddr_storage *lower,
                                        sockaddr_storage *upper);

#endif // __PREFIX_PARSER_H__
