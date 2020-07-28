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

#include "IPAllow.h"

#include "QUICApplication.h"

#include "HttpSessionAccept.h"

#include "Http3Types.h"
#include "Http3FrameDispatcher.h"
#include "Http3FrameCollector.h"
#include "Http3FrameGenerator.h"
#include "Http3FrameHandler.h"

class QUICNetVConnection;
class Http3Session;

/**
 * @brief A HTTP/3 application
 * @detail
 */
class Http3App : public QUICApplication
{
public:
  Http3App(QUICNetVConnection *client_vc, IpAllow::ACL &&session_acl, const HttpSessionAccept::Options &options);
  virtual ~Http3App();

  virtual void start();
  virtual int main_event_handler(int event, Event *data);

  // TODO: Return StreamIO. It looks bother that caller have to look up StreamIO by stream id.
  // Why not create_bidi_stream ?
  QUICConnectionErrorUPtr create_uni_stream(QUICStreamId &new_stream_id, Http3StreamType type);

protected:
  Http3Session *_ssn = nullptr;

private:
  void _handle_uni_stream_on_read_ready(int event, QUICStreamIO *stream_io);
  void _handle_uni_stream_on_write_ready(int event, QUICStreamIO *stream_io);
  void _handle_uni_stream_on_eos(int event, QUICStreamIO *stream_io);
  void _handle_bidi_stream_on_read_ready(int event, QUICStreamIO *stream_io);
  void _handle_bidi_stream_on_write_ready(int event, QUICStreamIO *stream_io);
  void _handle_bidi_stream_on_eos(int event, QUICStreamIO *stream_io);

  void _set_qpack_stream(Http3StreamType type, QUICStreamIO *stream_io);

  Http3FrameHandler *_settings_handler  = nullptr;
  Http3FrameGenerator *_settings_framer = nullptr;

  Http3FrameDispatcher _control_stream_dispatcher;
  Http3FrameCollector _control_stream_collector;

  QUICStreamIO *_remote_control_stream;
  QUICStreamIO *_local_control_stream;

  std::map<QUICStreamId, Http3StreamType> _remote_uni_stream_map;
  std::map<QUICStreamId, Http3StreamType> _local_uni_stream_map;
};

class Http3SettingsHandler : public Http3FrameHandler
{
public:
  Http3SettingsHandler(Http3Session *session) : _session(session){};

  // Http3FrameHandler
  std::vector<Http3FrameType> interests() override;
  Http3ErrorUPtr handle_frame(std::shared_ptr<const Http3Frame> frame) override;

private:
  // TODO: clarify Http3Session I/F for Http3SettingsHandler and Http3App
  Http3Session *_session = nullptr;
};

class Http3SettingsFramer : public Http3FrameGenerator
{
public:
  Http3SettingsFramer(NetVConnectionContext_t context) : _context(context){};

  // Http3FrameGenerator
  Http3FrameUPtr generate_frame(uint16_t max_size) override;
  bool is_done() const override;

private:
  NetVConnectionContext_t _context;
  bool _is_done = false; ///< Be careful when setting FIN flag on CONTROL stream. Maybe never?
  bool _is_sent = false; ///< Send SETTINGS frame only once
};
