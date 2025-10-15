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

#include "tscore/Hash.h"
#include <cstdint>

struct ATSHash64Wyhash : ATSHash64 {
  ATSHash64Wyhash();
  ATSHash64Wyhash(std::uint64_t seed);
  void          update(const void *data, std::size_t len) override;
  void          final() override;
  std::uint64_t get() const override;
  void          clear() override;

private:
  std::uint64_t seed      = 0;
  std::uint64_t state     = 0;
  std::uint64_t total_len = 0;
  std::uint64_t hfinal    = 0;
  bool          finalized = false;

  unsigned char buffer[32] = {0};
  std::uint8_t  buffer_len = 0;
};
