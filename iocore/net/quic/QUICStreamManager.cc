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

#include <QUICStreamManager.h>

#include <QUICApplication.h>
#include <P_QUICNetVConnection.h>

const static char *tag = "quic_stream_manager";

ClassAllocator<QUICStreamManager> quicStreamManagerAllocator("quicStreamManagerAllocator");
ClassAllocator<QUICStream> quicStreamAllocator("quicStreamAllocator");

int
QUICStreamManager::init(QUICFrameTransmitter *tx)
{
  this->_tx = tx;
  return 0;
}

void
QUICStreamManager::set_connection(QUICNetVConnection *vc)
{
  this->_vc = vc;
}

void
QUICStreamManager::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  switch (frame->type()) {
  case QUICFrameType::STREAM:
    this->_handle_stream_frame(std::dynamic_pointer_cast<const QUICStreamFrame>(frame));
    break;
  default:
    Debug(tag, "Unexpected frame type: %02x", frame->type());
    ink_assert(false);
    break;
  }
}

void
QUICStreamManager::_handle_stream_frame(std::shared_ptr<const QUICStreamFrame> frame)
{
  QUICStream *stream           = this->_find_or_create_stream(frame->stream_id());
  QUICApplication *application = this->_vc->get_application(frame->stream_id());

  if (!application->is_stream_set(stream)) {
    application->set_stream(stream);
  }

  stream->recv(frame);
  // FIXME: schedule VC_EVENT_READ_READY to application every single frame?
  // If application reading buffer continuously, do not schedule event.
  this_ethread()->schedule_imm(application, VC_EVENT_READ_READY, stream);

  return;
}

/**
 * @brief Send stream frame
 */
void
QUICStreamManager::send_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame)
{
  this->_tx->transmit_frame(std::move(frame));

  return;
}

QUICStream *
QUICStreamManager::_find_stream(QUICStreamId id)
{
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    if (s->id() == id) {
      return s;
    }
  }
  return nullptr;
}

QUICStream *
QUICStreamManager::_find_or_create_stream(QUICStreamId stream_id)
{
  QUICStream *stream = this->_find_stream(stream_id);
  if (!stream) {
    // TODO Free the stream somewhere
    stream = THREAD_ALLOC_INIT(quicStreamAllocator, this_ethread());
    stream->init(this, stream_id);
    stream->start();

    this->stream_list.push(stream);
  }
  return stream;
}
