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

/***************************************************************************
 This file contains constants and other limits used within the logging
 module.
 ***************************************************************************/

#pragma once

enum {
  LOG_MAX_FORMAT_LINE      = 2048, /* "format:enable:..." */
  LOG_MAX_FORMATTED_BUFFER = 20480,
  LOG_MAX_FORMATTED_LINE   = 10240
};

#define LOG_KILOBYTE ((int64_t)1024)
#define LOG_MEGABYTE ((int64_t)1048576)
