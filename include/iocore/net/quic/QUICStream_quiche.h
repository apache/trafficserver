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

#include "QUICStream.h"
#include <quiche.h>

class QUICStreamImpl : public QUICStream
{
public:
  QUICStreamImpl();
  QUICStreamImpl(QUICConnectionInfoProvider *cinfo, QUICStreamId sid);
  void receive_data(quiche_conn *quiche_con);
  void send_data(quiche_conn *quiche_con);

  // QUICStream
  virtual QUICOffset final_offset() const override;

  virtual void stop_sending(QUICStreamErrorUPtr error) override;
  virtual void reset(QUICStreamErrorUPtr error) override;

  virtual void on_read() override;
  virtual void on_eos() override;

  LINK(QUICStreamImpl, link);

private:
  uint64_t _received_bytes = 0;
  uint64_t _sent_bytes     = 0;
};
