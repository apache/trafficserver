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

#include "QUICHandshake.h"

#define I_WANNA_DUMP_THIS_BUF(buf, len)                                                                                           \
  {                                                                                                                               \
    int i, j;                                                                                                                     \
    fprintf(stderr, "len=%" PRId64 "\n", len);                                                                                    \
    for (i = 0; i < len / 8; i++) {                                                                                               \
      fprintf(stderr, "%02x %02x %02x %02x %02x %02x %02x %02x ", buf[i * 8 + 0], buf[i * 8 + 1], buf[i * 8 + 2], buf[i * 8 + 3], \
              buf[i * 8 + 4], buf[i * 8 + 5], buf[i * 8 + 6], buf[i * 8 + 7]);                                                    \
      if ((i + 1) % 4 == 0 || (len % 8 == 0 && i + 1 == len / 8)) {                                                               \
        fprintf(stderr, "\n");                                                                                                    \
      }                                                                                                                           \
    }                                                                                                                             \
    if (len % 8 != 0) {                                                                                                           \
      fprintf(stderr, "%0x", buf[i * 8 + 0]);                                                                                     \
      for (j = 1; j < len % 8; j++) {                                                                                             \
        fprintf(stderr, " %02x", buf[i * 8 + j]);                                                                                 \
      }                                                                                                                           \
      fprintf(stderr, "\n");                                                                                                      \
    }                                                                                                                             \
  }

const static char *tag                    = "quic_handshake";
const static int UDP_MAXIMUM_PAYLOAD_SIZE = 65527;
// TODO: fix size
const static int MAX_HANDSHAKE_MSG_LEN = 65527;

QUICHandshake::QUICHandshake(QUICConnection *qc, QUICCrypto *c) : QUICApplication(qc), _crypto(c)
{
  SET_HANDLER(&QUICHandshake::state_read_client_hello);
}

bool
QUICHandshake::is_completed()
{
  QUICCrypto *crypto = this->_crypto;
  return crypto->is_handshake_finished();
}

void
QUICHandshake::negotiated_application_name(const uint8_t **name, unsigned int *len)
{
  SSL_get0_alpn_selected(this->_crypto->ssl_handle(), name, len);
}

int
QUICHandshake::state_read_client_hello(int event, Event *data)
{
  QUICError error;
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    error = this->_process_client_hello();
    break;
  }
  default:
    Debug(tag, "event: %d", event);
    break;
  }

  if (error.cls != QUICErrorClass::NONE) {
    this->_client_qc->close(error);
    Debug(tag, "Enter state_closed");
    SET_HANDLER(&QUICHandshake::state_closed);
  }

  return EVENT_CONT;
}

int
QUICHandshake::state_read_client_finished(int event, Event *data)
{
  QUICError error;
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    error = this->_process_client_finished();
    break;
  }
  default:
    Debug(tag, "event: %d", event);
    break;
  }

  if (error.cls != QUICErrorClass::NONE) {
    this->_client_qc->close(error);
    Debug(tag, "Enter state_closed");
    SET_HANDLER(&QUICHandshake::state_closed);
  }

  return EVENT_CONT;
}

int
QUICHandshake::state_address_validation(int event, void *data)
{
  // TODO Address validation should be implemented for the 2nd implementation draft
  return EVENT_CONT;
}

int
QUICHandshake::state_complete(int event, void *data)
{
  Debug(tag, "event: %d", event);
  Debug(tag, "Got an event on complete state. Ignoring it for now.");

  return EVENT_CONT;
}

int
QUICHandshake::state_closed(int event, void *data)
{
  return EVENT_CONT;
}

QUICError
QUICHandshake::_process_client_hello()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);

  // Complete message should fit in a packet and be able to read
  uint8_t msg[UDP_MAXIMUM_PAYLOAD_SIZE] = {0};
  int64_t msg_len                       = stream_io->read_avail();
  stream_io->read(msg, msg_len);

  if (msg_len <= 0) {
    Debug(tag, "No message");
    return QUICError(QUICErrorClass::NONE);
  }

  // ----- DEBUG ----->
  I_WANNA_DUMP_THIS_BUF(msg, msg_len);
  // <----- DEBUG -----

  QUICCrypto *crypto = this->_crypto;

  uint8_t server_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t server_hello_len                     = 0;
  bool result                                 = false;
  result = crypto->handshake(server_hello, server_hello_len, MAX_HANDSHAKE_MSG_LEN, msg, msg_len);

  if (result) {
    // ----- DEBUG ----->
    I_WANNA_DUMP_THIS_BUF(server_hello, static_cast<int64_t>(server_hello_len));
    // <----- DEBUG -----

    Debug(tag, "Enter state_read_client_finished");
    SET_HANDLER(&QUICHandshake::state_read_client_finished);

    stream_io->write(server_hello, server_hello_len);
    stream_io->write_reenable();
    stream_io->read_reenable();

    return QUICError(QUICErrorClass::NONE);
  } else {
    return QUICError(QUICErrorClass::CRYPTOGRAPHIC, QUICErrorCode::TLS_HANDSHAKE_FAILED);
  }
}

QUICError
QUICHandshake::_process_client_finished()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);

  // Complete message should fit in a packet and be able to read
  uint8_t msg[UDP_MAXIMUM_PAYLOAD_SIZE] = {0};
  int64_t msg_len                       = stream_io->read_avail();
  stream_io->read(msg, msg_len);

  if (msg_len <= 0) {
    Debug(tag, "No message");
    return QUICError(QUICErrorClass::NONE);
  }

  // ----- DEBUG ----->
  I_WANNA_DUMP_THIS_BUF(msg, msg_len);
  // <----- DEBUG -----

  QUICCrypto *crypto = this->_crypto;

  uint8_t out[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t out_len                     = 0;
  bool result                        = false;
  result                             = crypto->handshake(out, out_len, MAX_HANDSHAKE_MSG_LEN, msg, msg_len);

  if (result) {
    // ----- DEBUG ----->
    I_WANNA_DUMP_THIS_BUF(out, static_cast<int64_t>(out_len));
    // <----- DEBUG -----

    ink_assert(this->is_completed());
    Debug(tag, "Handshake is completed");

    Debug(tag, "Enter state_complete");
    SET_HANDLER(&QUICHandshake::state_complete);
    _process_handshake_complete();

    stream_io->write(out, out_len);
    stream_io->write_reenable();
    stream_io->read_reenable();

    return QUICError(QUICErrorClass::NONE);
  } else {
    return QUICError(QUICErrorClass::CRYPTOGRAPHIC, QUICErrorCode::TLS_HANDSHAKE_FAILED);
  }
}

QUICError
QUICHandshake::_process_handshake_complete()
{
  QUICCrypto *crypto = this->_crypto;
  int r              = crypto->setup_session();

  if (r) {
    Debug(tag, "Keying Materials are exported");
  } else {
    Debug(tag, "Failed to export Keying Materials");
  }

  return QUICError(QUICErrorClass::NONE);
}
