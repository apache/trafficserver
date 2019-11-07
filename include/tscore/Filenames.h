/** @file

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

namespace ts
{
namespace filename
{
  constexpr const char *const STORAGE = "storage.config";
  constexpr const char *const RECORDS = "records.config";
  constexpr const char *const VOLUME  = "volume.config";
  constexpr const char *const PLUGIN  = "plugin.config";

  // These still need to have their corrensponding records.config settings remove
  constexpr const char *const LOGGING       = "logging.yaml";
  constexpr const char *const CACHE         = "cache.config";
  constexpr const char *const IP_ALLOW      = "ip_allow.yaml";
  constexpr const char *const HOSTING       = "hosting.config";
  constexpr const char *const SOCKS         = "socks.config";
  constexpr const char *const PARENT        = "parent.config";
  constexpr const char *const REMAP         = "remap.config";
  constexpr const char *const SSL_MULTICERT = "ssl_multicert.config";
  constexpr const char *const SPLITDNS      = "splitdns.config";
  constexpr const char *const SNI           = "sni.yaml";

  ///////////////////////////////////////////////////////////////////
  // Various other file names
  constexpr const char *const RECORDS_STATS = "records.snap";

} // namespace filename
} // namespace ts
