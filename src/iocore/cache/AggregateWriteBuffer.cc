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

#include "iocore/cache/AggregateWriteBuffer.h"

#include "iocore/aio/AIO_fault_injection.h"

#include "tscore/ink_assert.h"
#include "tscore/ink_platform.h"

bool
AggregateWriteBuffer::flush(int fd, off_t write_pos) const
{
  int r = pwrite(fd, this->_buffer, this->_buffer_pos, write_pos);
  if (r != this->_buffer_pos) {
    ink_assert(!"flushing agg buffer failed");
    return false;
  }
  return true;
}
