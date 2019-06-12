/** @file

  WebSocket termination example.

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

#pragma once

#include <string>

enum ws_frametype {
  WS_FRAME_CONTINUATION = 0x0,
  WS_FRAME_TEXT         = 0x1,
  WS_FRAME_BINARY       = 0x2,
  WS_FRAME_CLOSE        = 0x8,
  WS_FRAME_PING         = 0x9,
  WS_FRAME_PONG         = 0xA
};
typedef enum ws_frametype WS_FRAMETYPE;

#define WS_RSV1 0x40
#define WS_RSV2 0x20
#define WS_RSV3 0x10
#define WS_MASKED 0x80
#define WS_OPCODE 0x0F
#define WS_FIN 0x80
#define WS_LENGTH 0x7F
#define WS_16BIT_LEN 126
#define WS_64BIT_LEN 127

class WSBuffer
{
public:
  WSBuffer();

  /**
   * Adds incoming websocket data to the buffer for decoding.
   */
  void buffer(std::string const &data);

  /**
   * Returns a decoded message if there is sufficient data buffered.
   */
  bool read_buffered_message(std::string &message, int &code);

  /**
   * Calculates the Sec-WebSocket-Accept digest value for a given key.
   */
  static std::string ws_digest(std::string const &ws_key);

  /**
   * Convenience method returning a complete upgrade response.
   */
  static std::string get_handshake(std::string const &ws_key);

  /**
   * Gets the frame prefix for sending a message to the client.
   *
   * The complete message is: get_frame(msg.size(), code) + msg.
   */
  static std::string get_frame(size_t len, int code = WS_FIN + WS_FRAME_TEXT);

  /**
   * Gets the closing code and message if any.
   */
  static uint16_t get_closing_code(std::string const &message, std::string *desc = nullptr);

private:
  std::string ws_buf_;  // incoming data.
  int frame_ = 0;       // frame type of current message
  std::string msg_buf_; // decoded message data
};
