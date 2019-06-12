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

#include "WebSocket.h"

#include "tscpp/api/Logger.h"

// DISCLAIMER: this is intended for demonstration purposes only and
// does not pretend to implement a complete (or useful) server.

namespace
{
GlobalPlugin *plugin;
}

using namespace atscppapi;

void
TSPluginInit(int argc, const char *argv[])
{
  if (!RegisterGlobalPlugin("CPP_Example_WebSocket", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  plugin = new WebSocketInstaller(); // We keep a pointer to this so that clang-analyzer doesn't think it's a leak
}

// WebSocketInstaller

WebSocketInstaller::WebSocketInstaller() : GlobalPlugin(true /* ignore internal transactions */)
{
  GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
}

void
WebSocketInstaller::handleReadRequestHeadersPreRemap(Transaction &transaction)
{
  TS_DEBUG("websocket", "Incoming request.");
  transaction.addPlugin(new WebSocket(transaction));
  transaction.resume();
}

// WebSocket implementation.

WebSocket::WebSocket(Transaction &transaction) : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT)
{
  if (isWebsocket()) {
    TS_DEBUG("websocket", "WebSocket connection started.");
    ws_key_ = transaction.getClientRequest().getHeaders().values("sec-websocket-key");
    TS_DEBUG("websocket", "ws_key_ obtained");
  }
}

WebSocket::~WebSocket()
{
  TS_DEBUG("websocket", "WebSocket finished.");
}

void
WebSocket::consume(const std::string &data, InterceptPlugin::RequestDataType type)
{
  TS_DEBUG("websocket", "WebSocket consuming data");
  if (ws_key_.size()) {
    produce(WSBuffer::get_handshake(ws_key_));
    ws_key_ = "";
  }

  if (type == InterceptPlugin::REQUEST_HEADER) {
    headers_ += data;
  } else if (isWebsocket()) {
    int code;
    std::string message;
    ws_buf_.buffer(data);
    while (ws_buf_.read_buffered_message(message, code)) {
      ws_receive(message, code);
      if (code == WS_FRAME_CLOSE) {
        break;
      }
    }
  } else {
    body_ += data;
  }
}

void
WebSocket::ws_send(std::string const &msg, int code)
{
  produce(WSBuffer::get_frame(msg.size(), code) + msg);
}

void
WebSocket::ws_receive(std::string const &message, int code)
{
  switch (code) {
  case WS_FRAME_CLOSE:
    // NOTE: first two bytes (if sent) are a reason code
    // which we are expected to echo.
    if (message.size() > 2) {
      ws_send(message.substr(0, 2), WS_FIN + WS_FRAME_CLOSE);
    } else {
      ws_send("", WS_FIN + WS_FRAME_CLOSE);
    }
    setOutputComplete();
    break;
  case WS_FRAME_TEXT:
    TS_DEBUG("websocket", "WS client: %s", message.c_str());
    ws_send("got: " + message, WS_FIN + WS_FRAME_TEXT);
    break;
  case WS_FRAME_BINARY:
    TS_DEBUG("websocket", "WS client sent %d bytes", (int)message.size());
    ws_send("got binary data", WS_FIN + WS_FRAME_TEXT);
    break;
  case WS_FRAME_PING:
    TS_DEBUG("websocket", "WS client ping");
    ws_send(message, WS_FRAME_PONG);
    break;
  case WS_FRAME_CONTINUATION:
  // WSBuffer should not pass these on.
  case WS_FRAME_PONG:
  // We should not get these so just ignore.
  default:
    // Ignoring unrecognized opcodes.
    break;
  }
}

void
WebSocket::handleInputComplete()
{
  TS_DEBUG("websocket", "Request data complete (not a WebSocket connection).");

  std::string out = "HTTP/1.1 200 Ok\r\n"
                    "Content-type: text/plain\r\n"
                    "Content-length: 10\r\n"
                    "\r\n"
                    "Hi there!\n";
  produce(out);
  setOutputComplete();
}
