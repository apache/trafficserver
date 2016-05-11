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
#define SHOW(a) std::cout << #a << " = " << a << std::endl

enum ws_frametypes {
    WS_FRAME_CONTINUATION = 0x0,
    WS_FRAME_TEXT = 0x1,
    WS_FRAME_BINARY = 0x2,
    WS_FRAME_CLOSE = 0x8,
    WS_FRAME_PING = 0x9,
    WS_FRAME_PONG = 0xA
};
typedef enum ws_frametypes WS_FRAMETYPES;

#define WS_RSV1 0x40
#define WS_RSV2 0x20
#define WS_RSV3 0x10
#define WS_MASKED 0x80
#define WS_OPCODE 0x0F
#define WS_FIN 0x80
#define WS_LENGTH 0x7F
#define WS_16BIT_LEN 126
#define WS_64BIT_LEN 127

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

#define WS_DIGEST_MAX ATS_BASE64_ENCODE_DSTLEN(20)
static const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static std::string ws_digest(std::string const& key) {
    EVP_MD_CTX digest;
    EVP_MD_CTX_init(&digest);

    if (!EVP_DigestInit_ex(&digest, EVP_sha1(), NULL)) {
        EVP_MD_CTX_cleanup(&digest);
        return "init-failed";
    }
    if (!EVP_DigestUpdate(&digest, key.data(), key.length())) {
        EVP_MD_CTX_cleanup(&digest);
        return "update1-failed";
    }
    if (!EVP_DigestUpdate(&digest, magic.data(), magic.length())) {
        EVP_MD_CTX_cleanup(&digest);
        return "update2-failed";
    }

    unsigned char hash_buf[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    if (!EVP_DigestFinal_ex(&digest, hash_buf, &hash_len)) {
        EVP_MD_CTX_cleanup(&digest);
        return "final-failed";
    }
    EVP_MD_CTX_cleanup(&digest);
    if (hash_len != 20) {
        return "bad-hash-length";
    }

    char digest_buf[WS_DIGEST_MAX];
    size_t digest_len = 0;

    ats_base64_encode(hash_buf, hash_len, digest_buf, WS_DIGEST_MAX, &digest_len);

    return std::string((char*)digest_buf, digest_len);
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
    , pos_(0)
    , ctrl_(0)
    , mask_len_(0)
    , msg_len_(0)
    , ws_handshake_done_(false)
{
    if (isWebsocket()) {
        ws_key_ = transaction.getClientRequest().getHeaders().values("sec-websocket-key");
    }
}

WebSocket::~WebSocket()
{
    SAY("WebSocket finished.");
}

void WebSocket::ws_send(std::string const& data, int code)
{
    size_t len = data.length();

    std::string frame;
    frame.reserve(len + 10);
    frame += char(code);

    int len_len;
    if (len <= 125) {
        frame += char(len);
        len_len = 0;
    } else if (len <= UINT16_MAX) {
        frame += char(WS_16BIT_LEN);
        len_len = 2;
    } else {
        frame += char(WS_64BIT_LEN);
        len_len = 8;
    }
    // Convert length to big-endian bytes.
    while (--len_len >= 0) {
        frame += char((len >> (8*len_len)) & 0xFF);
    }

    frame += data;
    produce(frame);
}

bool WebSocket::read_buffered_message(std::string& message)
{
    // There are two basic states depending on whether or
    // not we have parsed a message length. If we're looking
    // for a message length, we don't advance pos_ until we
    // have read one (as well as control bytes and mask).
    //
    // Once we have a message length, we don't advance
    // pos_ until we have a complete message. (If the message
    // length is 0 we will produce the message immediately
    // and revert to looking for the next message length.)
    //
    // When incoming data is fragmented we may be called several
    // times before we get a length or a complete message.

    if (!msg_len_) {
        size_t avail = ws_buf_.size() - pos_;

        // Check if there is a mask (there should be).
        if (avail < 2) return false;
        mask_len_ = (ws_buf_[pos_] & WS_MASKED) ? 4 : 0;

        // Read the msg_length if we have enough data.
        if (avail < 2 + mask_len_) return false;
        ctrl_ = ws_buf_[pos_] & WS_OPCODE;

        size_t msg_len = ws_buf_[pos_ + 1] & WS_LENGTH;
        if (msg_len == WS_16BIT_LEN) {
            if (avail < 4 + mask_len_) { // 2 + 2 + length bytes + mask.
                return false;
            }
            pos_ += 2;
            msg_len = ntohs(*(uint16_t*)(ws_buf_.data() + pos_));
            pos_ += 2;
        } else if (msg_len == WS_64BIT_LEN) {
            if (avail < 10 + mask_len_) { // 2 + 8 length bytes + mask.
                return false;
            }
            pos_ += 2;
            msg_len = be64toh(*(uint64_t*)(ws_buf_.data() + pos_));
            pos_ += 8;
        } else {
            pos_ += 2;
        }
        msg_len_ = msg_len;

        // Copy any mask.
        for (size_t i = 0; i < mask_len_; ++i, ++pos_) {
            mask_[i] = ws_buf_[pos_];
        }
    }

    // Check if we have enough data to read the message.
    if (ws_buf_.size() < pos_ + msg_len_) return false; // not enough data.

    // Copy the message.
    message = ws_buf_.substr(pos_, msg_len_);

    // Apply any mask.
    if (mask_len_) {
        for (size_t i=0; i<msg_len_; ++i) {
            message[i] ^= mask_[i & 3];
        }
    }

    // Consume message and revert to looking for the next length.
    pos_ += msg_len_;
    msg_len_ = 0;

    return true;
}

void WebSocket::consume(const std::string &data, InterceptPlugin::RequestDataType type)
{
    if (isWebsocket()) {
        if (!ws_handshake_done_) {
            std::string digest = ws_digest(ws_key_);

            SAY("WS key digest: " << ws_key_ << ' ' << digest);

            // NOTE: a real server might be expecting a Sec-WebSocket-Protocol
            // header and wish to respond accordingly.

            std::string headers;
            headers +=
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + digest + "\r\n\r\n";
            produce(headers);
            ws_handshake_done_ = true;
        }
    }

    if (type == InterceptPlugin::REQUEST_HEADER) {
        headers_ += data;
    } else {
        if (isWebsocket()) {

            // Buffer the data.
            ws_buf_ += data;

            // Process as many messages as we can.
            bool done = false;
            std::string message;

            while (!done && read_buffered_message(message)) {

                switch (ctrl_) {
                case WS_FRAME_CONTINUATION:
                    // TODO: not implemented.
                    break;
                case WS_FRAME_CLOSE:
                    // NOTE: first two bytes (if sent) are a reason code
                    // which we are expected to echo.
                    if (message.size() > 2) {
                        message.erase(2);
                    } else {
                        message = "";
                    }
                    ws_send(message, WS_FIN + WS_FRAME_CLOSE);
                    setOutputComplete();
                    done = true;
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
            // Discard consumed data.
            if (pos_ > 0) {
                ws_buf_ = ws_buf_.substr(pos_);
                pos_ = 0;
            }
        } else {
            body_ += data;
        }
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
