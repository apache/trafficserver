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

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>

#include <ts/ts.h>

#define PLUGIN_NAME "protocol"

#define MAX_SERVER_NAME_LENGTH 1024
#define MAX_FILE_NAME_LENGTH 1024

/* MAX_SERVER_NAME_LENGTH + MAX_FILE_NAME_LENGTH + strlen("\n\n") */
#define MAX_REQUEST_LENGTH 2050

#define set_handler(_d, _s) \
  {                         \
    _d = _s;                \
  }
