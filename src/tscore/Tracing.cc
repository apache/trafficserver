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

#include "tscore/Tracing.h"

std::unique_ptr<Tracing> tracing = nullptr;

#if TS_USE_OPENTRACING
std::mutex tracers_mutex;
std::vector<std::shared_ptr<opentracing::Tracer>> tracers;
ink_thread_key thread_specific_tracer_key;
std::string ot_config;
opentracing::expected<opentracing::DynamicTracingLibraryHandle, std::error_code> ot_lib;
#endif

void
Tracing::enable(int value)
{
  this->_enabled = value;
}

void
Tracing::disable()
{
  this->_enabled = 0;
}
