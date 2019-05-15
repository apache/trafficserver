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

#include "QUICFrameGenerator.h"

void
QUICFrameGenerator::_records_frame(QUICFrameId id, QUICFrameInformationUPtr info)
{
  this->_info.insert(std::make_pair(id, std::move(info)));
}

bool
QUICFrameGenerator::_is_level_matched(QUICEncryptionLevel level)
{
  if (level == this->_encryption_level_filter) {
    return true;
  } else {
    return false;
  }
}

void
QUICFrameGenerator::on_frame_acked(QUICFrameId id)
{
  auto it = this->_info.find(id);
  if (it != this->_info.end()) {
    this->_on_frame_acked(it->second);
    this->_info.erase(it);
  }
}

void
QUICFrameGenerator::on_frame_lost(QUICFrameId id)
{
  auto it = this->_info.find(id);
  if (it != this->_info.end()) {
    this->_on_frame_lost(it->second);
    this->_info.erase(it);
  }
}
