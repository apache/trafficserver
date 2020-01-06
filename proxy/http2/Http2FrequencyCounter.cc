/** @file

  Http2FrequencyCounter

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

#include "Http2FrequencyCounter.h"

void
Http2FrequencyCounter::increment(uint16_t amount)
{
  ink_hrtime hrtime_sec = this->_get_hrtime();
  uint8_t counter_index = ((hrtime_sec % 60) >= 30);
  uint8_t last_index    = ((this->_last_update % 60) >= 30);

  if (hrtime_sec - this->_last_update > 60) {
    this->_count[0] = 0;
    this->_count[1] = 0;
  } else if (hrtime_sec - this->_last_update > 30) {
    if (counter_index == last_index) {
      this->_count[0] = 0;
      this->_count[1] = 0;
    } else {
      this->_count[counter_index] = 0;
    }
  } else if (counter_index != last_index) { // hrtime_sec - this->_last_update is less than 30
    this->_count[counter_index] = 0;
  }

  this->_count[counter_index] += amount;
  this->_last_update = hrtime_sec;
}

uint32_t
Http2FrequencyCounter::get_count()
{
  return this->_count[0] + this->_count[1];
}

ink_hrtime
Http2FrequencyCounter::_get_hrtime()
{
  return ink_hrtime_to_sec(Thread::get_hrtime());
}
