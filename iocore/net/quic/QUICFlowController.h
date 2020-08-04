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

#include "I_EventSystem.h"
#include "QUICTypes.h"
#include "QUICFrame.h"
#include "QUICFrameGenerator.h"
#include "QUICLossDetector.h"

class QUICRateAnalyzer
{
public:
  void update(QUICOffset offset);
  uint64_t expect_recv_bytes(ink_hrtime time) const;

private:
  double _rate           = 0.0;
  ink_hrtime _start_time = Thread::get_hrtime();
};

class QUICFlowController : public QUICFrameGenerator
{
public:
  uint64_t credit() const;
  QUICOffset current_offset() const;
  virtual QUICOffset current_limit() const;

  /*
   * Returns 0 if succeed
   */
  virtual int update(QUICOffset offset);
  virtual void forward_limit(QUICOffset limit);

  /**
   * This is only for flow controllers initialized without a limit (== UINT64_MAX).
   * Once a limit is set, it should be updated with forward_limit().
   */
  virtual void set_limit(QUICOffset limit);

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            size_t current_packet_size, uint32_t seq_num) override;

protected:
  QUICFlowController(uint64_t initial_limit) : _limit(initial_limit) {}
  virtual QUICFrame *_create_frame(uint8_t *buf) = 0;

  QUICOffset _offset        = 0; //< Largest sent/received offset
  QUICOffset _limit         = 0; //< Maximum amount of data to send/receive
  bool _should_create_frame = false;
};

class QUICRemoteFlowController : public QUICFlowController
{
public:
  QUICRemoteFlowController(uint64_t initial_limit) : QUICFlowController(initial_limit) {}
  int update(QUICOffset offset) override;
  void forward_limit(QUICOffset new_limit) override;

private:
  void _on_frame_lost(QUICFrameInformationUPtr &info) override;
  bool _blocked = false;
};

class QUICLocalFlowController : public QUICFlowController
{
public:
  QUICLocalFlowController(QUICRTTProvider *rtt_provider, uint64_t initial_limit)
    : QUICFlowController(initial_limit), _rtt_provider(rtt_provider)
  {
  }
  QUICOffset current_limit() const override;

  /**
   * Unlike QUICRemoteFlowController::forward_limit(), this function forwards limit if needed.
   */
  void forward_limit(QUICOffset new_limit) override;
  int update(QUICOffset offset) override;
  void set_limit(QUICOffset limit) override;

private:
  void _on_frame_lost(QUICFrameInformationUPtr &info) override;
  bool _need_to_forward_limit();

  QUICRateAnalyzer _analyzer;
  QUICRTTProvider *_rtt_provider = nullptr;
};

class QUICRemoteConnectionFlowController : public QUICRemoteFlowController
{
public:
  QUICRemoteConnectionFlowController(uint64_t initial_limit) : QUICRemoteFlowController(initial_limit) {}
  QUICFrame *_create_frame(uint8_t *buf) override;
};

class QUICLocalConnectionFlowController : public QUICLocalFlowController
{
public:
  QUICLocalConnectionFlowController(QUICRTTProvider *rtt_provider, uint64_t initial_limit)
    : QUICLocalFlowController(rtt_provider, initial_limit)
  {
  }
  QUICFrame *_create_frame(uint8_t *buf) override;
};

class QUICRemoteStreamFlowController : public QUICRemoteFlowController
{
public:
  QUICRemoteStreamFlowController(uint64_t initial_limit, QUICStreamId stream_id)
    : QUICRemoteFlowController(initial_limit), _stream_id(stream_id)
  {
  }
  QUICFrame *_create_frame(uint8_t *buf) override;

private:
  QUICStreamId _stream_id = 0;
};

class QUICLocalStreamFlowController : public QUICLocalFlowController
{
public:
  QUICLocalStreamFlowController(QUICRTTProvider *rtt_provider, uint64_t initial_limit, QUICStreamId stream_id)
    : QUICLocalFlowController(rtt_provider, initial_limit), _stream_id(stream_id)
  {
  }
  QUICFrame *_create_frame(uint8_t *buf) override;

private:
  QUICStreamId _stream_id = 0;
};
