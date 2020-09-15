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

#include "QUICApplication.h"
#include "Http3Frame.h"
#include "Http3FrameGenerator.h"
#include <vector>

class QUICStreamVCAdapter;

class Http3FrameCollector
{
public:
  Http3ErrorUPtr on_write_ready(QUICStreamId stream_id, MIOBuffer &writer, size_t &nread, bool &all_done);

  void add_generator(Http3FrameGenerator *generator);

private:
  std::vector<Http3FrameGenerator *> _generators;
};
