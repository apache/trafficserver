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

#include <random>

namespace ts
{
class Random
{
public:
  Random() = delete;

  static uint64_t
  random()
  {
    return _state.int_dist(_state.engine);
  }

  static double
  drandom()
  {
    return _state.real_dist(_state.engine);
  }

  static void
  seed(uint64_t s)
  {
    _state.engine.seed(s);
  }

private:
  struct State {
    std::mt19937_64                         engine{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> int_dist{0, UINT64_MAX};
    std::uniform_real_distribution<double>  real_dist{0.0, 1.0};
  };

  thread_local static State _state;
};
}; // namespace ts
