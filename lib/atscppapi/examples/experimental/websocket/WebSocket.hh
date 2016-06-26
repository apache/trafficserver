#ifndef WEBSOCKET_HEADER_HH_
#define WEBSOCKET_HEADER_HH_

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>

#include <string>
#include <stddef.h>

#include "WSBuffer.hh"

// WebSocket InterceptPlugin

using atscppapi::InterceptPlugin;
using atscppapi::Transaction;
using atscppapi::GlobalPlugin;

class WebSocket : public InterceptPlugin
{
public:
    WebSocket(Transaction& transaction);
    ~WebSocket();

    void consume(const std::string &data, InterceptPlugin::RequestDataType type);
    void handleInputComplete();

    void ws_send(std::string const& data, int code);
    void ws_receive(std::string const& data, int code);

private:
    std::string headers_;
    std::string body_;

    std::string ws_key_; // value of sec-websocket-key header
    WSBuffer ws_buf_;    // incoming data.
};

class WebSocketInstaller : public GlobalPlugin
{
public:
    WebSocketInstaller();

    void handleReadRequestHeadersPreRemap(Transaction &transaction);
};

#endif
