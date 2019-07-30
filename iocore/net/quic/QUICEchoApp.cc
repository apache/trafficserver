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

#include "QUICEchoApp.h"

#include "P_Net.h"
#include "P_VConnection.h"
#include "QUICDebugNames.h"

static constexpr char tag[] = "quic_echo_app";

QUICEchoApp::QUICEchoApp(QUICConnection *qc) : QUICApplication(qc)
{
  SET_HANDLER(&QUICEchoApp::main_event_handler);
}

int
QUICEchoApp::main_event_handler(int event, Event *data)
{
  Debug(tag, "%s", get_vc_event_name(event));

  QUICStream *stream      = reinterpret_cast<QUICStream *>(data->cookie);
  QUICStreamIO *stream_io = this->_find_stream_io(stream->id());
  if (stream_io == nullptr) {
    Debug(tag, "Unknown Stream, id: %" PRIx64, stream->id());
    return -1;
  }

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    uint8_t msg[1024] = {0};
    int64_t msg_len   = 1024;

    int64_t read_len = stream_io->read(msg, msg_len);

    if (read_len) {
      Debug(tag, "msg: %s, len: %" PRId64, msg, read_len);

      stream_io->write(msg, read_len);
      stream_io->write_reenable();
      stream_io->read_reenable();
    } else {
      Debug(tag, "No MSG");
    }
    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    // do nothing
    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT: {
    ink_assert(false);
    break;
  }
  default:
    break;
  }

  return EVENT_CONT;
}
