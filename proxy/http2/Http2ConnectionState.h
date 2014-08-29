/** @file

  Http2ConnectionState.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#ifndef __HTTP2_CONNECTION_STATE_H__
#define __HTTP2_CONNECTION_STATE_H__

#include "HTTP2.h"

class Http2ClientSession;

class Http2ConnectionSettings
{
public:
  unsigned get(Http2SettingsIdentifier id) const {
    return this->settings[indexof(id)];
  }

  unsigned set(Http2SettingsIdentifier id, unsigned value) {
    return this->settings[indexof(id)] = value;
  }

private:

  // Settings ID is 1-based, so convert it to a 0-based index.
  static unsigned indexof(Http2SettingsIdentifier id) {
    return id - 1;
  }

  unsigned settings[HTTP2_SETTINGS_MAX - 1];
};

// Http2ConnectionState
//
// Capture the semantics of a HTTP/2 connection. The client session captures the frame layer, and the
// connection state captures the connection-wide state.

class Http2ConnectionState : public Continuation
{
public:

  Http2ConnectionState() : Continuation(NULL), ua_session(NULL) {
    SET_HANDLER(&Http2ConnectionState::main_event_handler);
  }

  Http2ClientSession * ua_session;

  // Settings.
  Http2ConnectionSettings server_settings;
  Http2ConnectionSettings client_settings;

  int main_event_handler(int, void *);
  int state_closed(int, void *);

private:
  Http2ConnectionState(const Http2ConnectionState&); // noncopyable
  Http2ConnectionState& operator=(const Http2ConnectionState&); // noncopyable
};

#endif // __HTTP2_CONNECTION_STATE_H__
