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

class QUICStreamAdapter
{
public:
  QUICStreamAdapter(QUICStream &stream) : _stream(stream) {}
  virtual ~QUICStreamAdapter() = default;

  QUICStream &
  stream()
  {
    return _stream;
  }

  virtual int64_t write(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin) = 0;
  Ptr<IOBufferBlock> read(size_t len);
  virtual bool is_eos()         = 0;
  virtual uint64_t unread_len() = 0;
  virtual uint64_t read_len()   = 0;
  virtual uint64_t total_len()  = 0;

  /**
   * Tell the application that there is data to read
   */
  virtual void encourge_read() = 0;

  /**
   * Tell the application that there is some space to write data
   */
  virtual void encourge_write() = 0;

  /**
   * Tell the application that there is no more data to read
   */
  virtual void notify_eos() = 0;

protected:
  virtual Ptr<IOBufferBlock> _read(size_t len) = 0;
  QUICStream &_stream;
};
