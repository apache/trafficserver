/**
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

/**
 * @file HttpVersion.h
 * @brief Contains an enumeration and printable strings for Http Versions.
 */

#pragma once

#include <string>

namespace atscppapi
{
/**
 * An enumeration of all available Http Versions.
 */
enum HttpVersion {
  HTTP_VERSION_UNKNOWN = 0,
  HTTP_VERSION_0_9,
  HTTP_VERSION_1_0,
  HTTP_VERSION_1_1,
};

/**
 * An array of printable strings representing of the HttpVersion
 * \code
 * cout << HTTP_VERSION_STRINGS[HTTP_VERSION_1_1] << endl;
 * \endcode
 */
extern const std::string HTTP_VERSION_STRINGS[];
} // namespace atscppapi
