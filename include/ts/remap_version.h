/** @file

  Remap API version check.

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

#define CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errlen)                                                                  \
  do {                                                                                                                           \
    if (api_info == nullptr) {                                                                                                   \
      snprintf(errbuf, errlen, "Missing TSRemapInterface argument");                                                             \
      return TS_ERROR;                                                                                                           \
    }                                                                                                                            \
    if (api_info->size < sizeof(TSRemapInterface)) {                                                                             \
      snprintf(errbuf, errlen, "Incorrect size (%zu) of TSRemapInterface structure, expected %zu",                               \
               static_cast<size_t>(api_info->size), sizeof(TSRemapInterface));                                                   \
      return TS_ERROR;                                                                                                           \
    }                                                                                                                            \
    if (api_info->tsremap_version < TSREMAP_VERSION) {                                                                           \
      snprintf(errbuf, errlen, "Incorrect API version %d.%d, expected %d.%d", static_cast<int>(api_info->tsremap_version >> 16), \
               static_cast<int>(api_info->tsremap_version & 0xffff), static_cast<int>(TSREMAP_VMAJOR),                           \
               static_cast<int>(TSREMAP_VMINOR));                                                                                \
      return TS_ERROR;                                                                                                           \
    }                                                                                                                            \
  } while (0)
