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

#include <map>
#include <queue>

#include "QUICTypes.h"
#include "QUICFrame.h"
#include "QUICTransferProgressProvider.h"

class QUICIncomingFrameBuffer
{
public:
  virtual QUICFrameSPtr pop() = 0;
  /*
   * Becasue frames passed by FrameDispatcher is temporal, this clones a passed frame to ensure that we can use it later.
   */
  virtual QUICErrorUPtr insert(const QUICFrame &frame) = 0;
  virtual void clear();
  virtual bool empty();

protected:
  QUICOffset _recv_offset = 0;

  std::queue<QUICFrameSPtr> _recv_buffer;
  std::map<QUICOffset, QUICFrameSPtr> _out_of_order_queue;
};

class QUICIncomingStreamFrameBuffer : public QUICIncomingFrameBuffer, public QUICTransferProgressProvider
{
public:
  using super = QUICIncomingFrameBuffer; ///< Parent type.

  QUICIncomingStreamFrameBuffer(const QUICStream *stream) : _stream(stream) {}
  ~QUICIncomingStreamFrameBuffer();

  QUICFrameSPtr pop() override;
  QUICErrorUPtr insert(const QUICFrame &frame) override;
  void clear() override;

  // QUICTransferProgressProvider
  bool is_transfer_goal_set() const override;
  bool is_transfer_complete() const override;
  uint64_t transfer_progress() const override;
  uint64_t transfer_goal() const override;

private:
  QUICErrorUPtr _check_and_set_fin_flag(QUICOffset offset, size_t len = 0, bool fin_flag = false);

  const QUICStream *_stream = nullptr;
  QUICOffset _max_offset    = 0;
  QUICOffset _fin_offset    = UINT64_MAX;
};

class QUICIncomingCryptoFrameBuffer : public QUICIncomingFrameBuffer
{
public:
  using super = QUICIncomingFrameBuffer; ///< Parent type.

  QUICIncomingCryptoFrameBuffer() {}
  ~QUICIncomingCryptoFrameBuffer();

  QUICFrameSPtr pop() override;
  QUICErrorUPtr insert(const QUICFrame &frame) override;

private:
};
