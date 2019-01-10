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

#include "QUICFrame.h"
#include "QUICFrameRetransmitter.h"

class QUICFrameGenerator
{
public:
  virtual ~QUICFrameGenerator(){};
  virtual bool will_generate_frame(QUICEncryptionLevel level)                                                              = 0;
  virtual QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) = 0;

  void on_frame_acked(QUICFrameId id);
  void on_frame_lost(QUICFrameId id);

protected:
  QUICFrameId
  _issue_frame_id()
  {
    return this->_latest_frame_Id++;
  }

  virtual void
  _on_frame_acked(QUICFrameInformationUPtr &info)
  {
  }

  virtual void
  _on_frame_lost(QUICFrameInformationUPtr &info)
  {
  }

  virtual std::vector<QUICEncryptionLevel> _encryption_level_filter();
  virtual bool _is_level_matched(QUICEncryptionLevel level);
  void _records_frame(QUICFrameId id, QUICFrameInformationUPtr info);

private:
  QUICFrameId _latest_frame_Id = 0;
  std::map<QUICFrameId, QUICFrameInformationUPtr> _info;
};
