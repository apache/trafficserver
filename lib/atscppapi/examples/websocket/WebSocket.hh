#ifndef WEBSOCKET_HEADER_HH_
#define WEBSOCKET_HEADER_HH_

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>

#include <string>
#include <stddef.h>

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

    void ws_send(std::string const& data, int code=0x81);

private:
    std::string headers_;
    std::string body_;

    std::string ws_buf_; // incoming data.
    size_t pos_;         // next byte to read in ws_buf_
    int ctrl_;
    char mask_[4];
    size_t msg_len_;     // length of current message
    bool ws_handshake_done_;
    std::string ws_key_; // value of sec-websocket-key header
};

class WebSocketInstaller : public GlobalPlugin
{
public:
    WebSocketInstaller();

    void handleReadRequestHeadersPreRemap(Transaction &transaction);
};

#endif
