#include "WSBuffer.hh"

#include "ts/ink_base64.h"
#include "openssl/evp.h"
#include <netinet/in.h>
#include <arpa/inet.h>

#define WS_DIGEST_MAX ATS_BASE64_ENCODE_DSTLEN(20)
static const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WSBuffer::WSBuffer()
    : pos_(0)
    , ctrl_(0)
    , mask_len_(0)
    , msg_len_(0)
{
}

void WSBuffer::buffer(std::string const& data)
{
    ws_buf_ += data;
}

bool WSBuffer::read_buffered_message(std::string& message, int& code)
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
    code = ctrl_;

    // Apply any mask.
    if (mask_len_) {
        for (size_t i=0; i<msg_len_; ++i) {
            message[i] ^= mask_[i & 3];
        }
    }

    // Consume message and revert to looking for the next length.
    pos_ += msg_len_;
    msg_len_ = 0;

    // Discard consumed data.
    if (pos_ > 0) {
        ws_buf_ = ws_buf_.substr(pos_);
        pos_ = 0;
    }

    return true;
}

std::string WSBuffer::ws_digest(std::string const& key) {
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

std::string WSBuffer::get_handshake(std::string const& ws_key)
{
    std::string digest = ws_digest(ws_key);

    // NOTE: a real server might be expecting a Sec-WebSocket-Protocol
    // header and wish to respond accordingly.

    std::string headers;
    headers +=
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + digest + "\r\n\r\n";
    return headers;
}

std::string WSBuffer::get_frame(size_t len, int code)
{
    std::string frame;
    frame.reserve(10);
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

    return frame;
}
