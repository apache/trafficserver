/** @file

  Pseudorandom Number Generator

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

#include "tscore/Random.h"

namespace ts
{
thread_local std::mt19937_64 Random::_engine{std::random_device{}()};
thread_local std::uniform_int_distribution<uint64_t> Random::_dist_int{0, UINT64_MAX};
thread_local std::uniform_real_distribution<double> Random::_dist_real{0, 1};
}; // namespace ts
