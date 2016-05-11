#include "WebSocket.hh"

#include <iostream>
#include "ts/ink_base64.h"
#include "openssl/evp.h"
#include <netinet/in.h>
#include <arpa/inet.h>

// DISCLAIMER: this is intended for demonstration purposes only and
// does not pretend to implement a complete (or useful) server.

using namespace atscppapi;

#define SAY(a) std::cout << a << std::endl;

void TSPluginInit(int argc, const char* argv[])
{
    RegisterGlobalPlugin("WebSocket", "Apache", "support@example.com");
    new WebSocketInstaller();
}

// WebSocketInstaller

WebSocketInstaller::WebSocketInstaller()
    : GlobalPlugin(true /* ignore internal transactions */)
{
    GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
}

void WebSocketInstaller::handleReadRequestHeadersPreRemap(Transaction &transaction)
{
    SAY("Incoming request.");
    transaction.addPlugin(new WebSocket(transaction));
    transaction.resume();
}

// WebSocket implementation.

WebSocket::WebSocket(Transaction& transaction)
    : InterceptPlugin(transaction, InterceptPlugin::SERVER_INTERCEPT)
{
    if (isWebsocket()) {
        ws_key_ = transaction.getClientRequest().getHeaders().values("sec-websocket-key");
    }
}

WebSocket::~WebSocket()
{
    SAY("WebSocket finished.");
}

void WebSocket::consume(const std::string &data, InterceptPlugin::RequestDataType type)
{
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
            if (code == WS_FRAME_CLOSE) break;
        }
    } else {
        body_ += data;
    }
}

void WebSocket::ws_send(std::string const& msg, int code)
{
    produce(WSBuffer::get_frame(msg.size(), code) + msg);
}

void WebSocket::ws_receive(std::string const& message, int code)
{
    switch (code) {
    case WS_FRAME_CONTINUATION:
        // TODO: not implemented.
        break;
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
        SAY("WS client: " << message);
        ws_send("got: " + message, WS_FIN + WS_FRAME_TEXT);
        break;
    case WS_FRAME_BINARY:
        SAY("WS client sent " << message.size() << " bytes");
        ws_send("got binary data", WS_FIN + WS_FRAME_TEXT);
        break;
    case WS_FRAME_PING:
        SAY("WS client ping");
        ws_send(message, WS_FRAME_PONG);
        break;
    case WS_FRAME_PONG:
        break;
    default:
        // Ignoring unrecognized opcodes.
        break;
    }
}

void WebSocket::handleInputComplete()
{
    SAY("Request data complete (not a WebSocket connection).");

    std::string out = \
        "HTTP/1.1 200 Ok\r\n"
        "Content-type: text/plain\r\n"
        "Content-length: 10\r\n"
        "\r\n"
        "Hi there!\n";
    produce(out);
    setOutputComplete();
}
