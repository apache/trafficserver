/** @file

  Zstd compression implementation

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information regarding copyright ownership.  The ASF licenses this file to you under
  the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License.  You may
  obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS
  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the License for the specific
  language governing permissions and limitations under the License.
 */

#pragma once

#include "tscore/ink_config.h"
#include "compress_common.h"

#if HAVE_ZSTD_H

namespace Zstd
{
// Initialize Zstd compression context
void data_alloc(Data *data);

// Destroy Zstd compression context
void data_destroy(Data *data);

// Configure the context just before streaming starts. Returns true when ready.
bool transform_init(Data *data);

// Compress one upstream chunk
void transform_one(Data *data, const char *upstream_buffer, int64_t upstream_length);

// Finish compression and flush remaining data
void transform_finish(Data *data);
} // namespace Zstd

#endif // HAVE_ZSTD_H
