/** @file

  Multiplexes request to other origins.

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

#include <ts/ts.h>

#include "dispatch.h"

struct PostState {
  Requests requests;

  /// The Content-Length value of the POST/PUT request.
  int content_length = -1;

  TSIOBuffer       origin_buffer;
  TSIOBufferReader clone_reader;
  /// The VIO for the original (non-clone) origin.
  TSVIO output_vio;

  ~PostState();
  PostState(Requests &, int content_length);
};

int handlePost(TSCont, TSEvent, void *);
