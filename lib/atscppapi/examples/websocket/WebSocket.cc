#include "WebSocket.hh"

#include <iostream>
#include "ts/ink_base64.h"
#include "openssl/evp.h"

using namespace atscppapi;

#define SAY(a) std::cout << a << std::endl;
#define SHOW(a) std::cout << #a << " = " << a << std::endl

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
    , msg_len_(0)
    , ws_handshake_done_(false)
{
    ws_key_ = transaction.getClientRequest().getHeaders().values("sec-websocket-key");
    if (ws_key_.size()) {
        printf("setting timeouts\n");
        transaction.setTimeout(Transaction::TIMEOUT_ACTIVE, 844000000);
        transaction.setTimeout(Transaction::TIMEOUT_NO_ACTIVITY, 84400000);
    }
}

WebSocket::~WebSocket()
{
    SAY("WebSocket finished.");
}

void WebSocket::ws_send(std::string const& data, int code)
{
    std::string frame(1, char(code));
    size_t len = data.length();
    if (len <= 125) {
        frame += char(len);
    } else {
        int len_len;
        if (len <= 65535) {
            frame += char(126);
            len_len = 2;
        } else {
            frame += char(127);
            len_len = 8;
        }
        while (--len_len >= 0) {
            frame += char((len >> (8*len_len)) & 0xFF);
        }
    }
    produce(frame + data);
}

void WebSocket::consume(const std::string &data, InterceptPlugin::RequestDataType type)
{
    if (isWebsocket()) {
        if (!ws_handshake_done_) {
            std::string digest = ws_digest(ws_key_);

            printf("WS key digest: %s %s\n", ws_key_.c_str(), digest.c_str());

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

    // auto tid = std::this_thread::get_id();
    if (type == InterceptPlugin::REQUEST_HEADER) {
        headers_ += data;
    } else {
        // cout << tid << ": Read request body" << endl << data << endl;
        if (isWebsocket()) {

	    // When we've read as much as we can for any incoming
	    // data chunk we remove the head of ws_buf_ and reset pos_
	    // to 0.
	    //
	    // If we can't read a complete length + masks, we leave
	    // pos_ unchanged and just append what we got to ws_buf_.
	    //
	    // When we get the length, we update pos_ and length.
	    // While length is non-zero we wait for ws_buf_ to have
	    // msg_len_ bytes beyond pos_.

            ws_buf_ += data;
            while (true) {
                if (!msg_len_) {
                    size_t avail = ws_buf_.size() - pos_;

                    // Read the msg_length if we have enough data.
                    if (avail < 6) break; // 2 + 4 mask
                    ctrl_ = ws_buf_[pos_] & 0xF;
                    size_t msg_len = ws_buf_[pos_ + 1] & 0x7F;
                    if (msg_len == 0x7E) {
                        if (avail < 8) { // 2 + 2 + length bytes + 4 mask.
                            break;
                        }
                        msg_len = 0;
                        pos_ += 2;
                        for (int i = 0; i < 2; ++i, ++pos_) {
                            msg_len <<= 8;
                            msg_len += (unsigned char)(ws_buf_[pos_]);
                        }
                    } else if (msg_len == 0x7F) {
                        if (avail < 14) { // 2 + 8 length bytes + 4 mask.
                            break;
                        }
                        msg_len = 0;
                        pos_ += 2;
                        for (int i = 0; i < 8; ++i, ++pos_) {
                            msg_len <<= 8;
                            msg_len += (unsigned char)(ws_buf_[pos_]);
                        }
                    } else {
                        pos_ += 2;
                    }
                    msg_len_ = msg_len;
                    // Copy the mask.
                    for (size_t i = 0; i < 4; ++i, ++pos_) {
                        mask_[i] = ws_buf_[pos_];
                    }
                }

                // Check if we have enough data to read the message.
                if (ws_buf_.size() < pos_ + msg_len_) break; // not enough data.

                // Apply the mask.
                for (size_t i=0; i<msg_len_; ++i) {
                    ws_buf_[pos_ + i] ^= mask_[i & 3];
                }

                std::string message = ws_buf_.substr(pos_, msg_len_);

                if (ctrl_ == 0x8) {
                    if (message.size() > 2) {
                        message.erase(0, 2);
                    } else {
                        message = "";
                    }
                    ws_send(message, 0x88);

                    SAY("WS client CLOSE");
                    setOutputComplete();
                    break;
                }

                SAY("WS client: " << message);
                ws_send("got: " + message);

                pos_ += msg_len_;
                msg_len_ = 0;
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
