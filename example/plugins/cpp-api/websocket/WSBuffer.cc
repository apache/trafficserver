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

#include "WSBuffer.h"

#include <ts/ts.h>
#include "tscore/ink_config.h"
#include "openssl/evp.h"
#include <netinet/in.h>
#include <arpa/inet.h>

#if defined(__linux__)
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(sun) || defined(__sun)
#if BYTE_ORDER == LITTLE_ENDIAN
#define be64toh(x) ntohll(x)
#elif BYTE_ORDER == BIG_ENDIAN
#define be64toh(x) (x)
#endif
#elif defined(__OpenBSD__) || defined(__NetBSD__) || defined(__FreeBSD__)
#include <sys/endian.h>
#elif defined(__DragonFly__)
#include <machine/endian.h>
#if BYTE_ORDER == LITTLE_ENDIAN
#define be64toh(x) __bswap64(x)
#elif BYTE_ORDER == BIG_ENDIAN
#define be64toh(x) (x)
#endif
#endif

#define BASE64_ENCODE_DSTLEN(_length) ((_length * 8) / 6 + 4)
#define WS_DIGEST_MAX BASE64_ENCODE_DSTLEN(20)

static const std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WSBuffer::WSBuffer() {}

void
WSBuffer::buffer(std::string const &data)
{
  ws_buf_ += data;
}

bool
WSBuffer::read_buffered_message(std::string &message, int &code)
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

  char mask[4];

  size_t avail = ws_buf_.size();

  // Check if there is a mask (there should be).
  if (avail < 2) {
    return false;
  }
  size_t mask_len = (ws_buf_[1] & WS_MASKED) ? 4 : 0;

  int frame  = ws_buf_[0] & WS_OPCODE;
  bool first = frame != WS_FRAME_CONTINUATION;
  auto final = ws_buf_[0] & WS_FIN;

  // Save/restore frame type on first/continuation.
  if (first) {
    frame_ = frame;
    msg_buf_.clear();
  } else {
    frame = frame_;
  }

  // Read the msg_length if we have enough data.
  if (avail < 2 + mask_len) {
    return false;
  }

  size_t msg_len = ws_buf_[1] & WS_LENGTH;
  size_t pos;
  if (msg_len == WS_16BIT_LEN) {
    if (avail < 4 + mask_len) { // 2 + 2 + length bytes + mask.
      return false;
    }
    msg_len = ntohs(*reinterpret_cast<uint16_t *>(ws_buf_.data() + 2));
    pos     = 4;
  } else if (msg_len == WS_64BIT_LEN) {
    if (avail < 10 + mask_len) { // 2 + 8 length bytes + mask.
      return false;
    }
    msg_len = be64toh(*(uint64_t *)(ws_buf_.data() + 2));
    pos     = 10;
  } else {
    pos = 2;
  }

  // Check if we have enough data to read the message.
  if (ws_buf_.size() < pos + msg_len) {
    return false; // not enough data.
  }

  // Copy any mask.
  for (size_t i = 0; i < mask_len; ++i, ++pos) {
    mask[i] = ws_buf_[pos];
  }

  // Apply any mask.
  if (mask_len) {
    for (size_t i = 0, p = pos; i < msg_len && p < ws_buf_.size(); ++i, ++p) {
      ws_buf_[p] ^= mask[i & 3];
    }
  }

  // Copy the message out.
  if (final) {
    message = msg_buf_;
    message += ws_buf_.substr(pos, msg_len);
    code = frame;
  } else {
    msg_buf_ += ws_buf_.substr(pos, msg_len);
  }

  // Discard consumed data.
  ws_buf_.erase(0, pos + msg_len);

  return true;
}

std::string
WSBuffer::ws_digest(std::string const &key)
{
  EVP_MD_CTX *digest = EVP_MD_CTX_new();

  if (!EVP_DigestInit_ex(digest, EVP_sha1(), nullptr)) {
    EVP_MD_CTX_free(digest);
    return "init-failed";
  }
  if (!EVP_DigestUpdate(digest, key.data(), key.length())) {
    EVP_MD_CTX_free(digest);
    return "update1-failed";
  }
  if (!EVP_DigestUpdate(digest, magic.data(), magic.length())) {
    EVP_MD_CTX_free(digest);
    return "update2-failed";
  }

  unsigned char hash_buf[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  if (!EVP_DigestFinal_ex(digest, hash_buf, &hash_len)) {
    EVP_MD_CTX_free(digest);
    return "final-failed";
  }
  EVP_MD_CTX_free(digest);
  if (hash_len != 20) {
    return "bad-hash-length";
  }

  char digest_buf[WS_DIGEST_MAX];
  size_t digest_len = 0;

  if (TSBase64Encode(reinterpret_cast<char *>(hash_buf), hash_len, digest_buf, WS_DIGEST_MAX, &digest_len) != TS_SUCCESS) {
    return "base64encode-failed";
  }

  return std::string((char *)digest_buf, digest_len);
}

std::string
WSBuffer::get_handshake(std::string const &ws_key)
{
  std::string digest = ws_digest(ws_key);

  // NOTE: a real server might be expecting a Sec-WebSocket-Protocol
  // header and wish to respond accordingly. In that case you must
  // call ws_digest() and construct the headers yourself.

  std::string headers = "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: " +
                        digest + "\r\n\r\n";
  return headers;
}

std::string
WSBuffer::get_frame(size_t len, int code)
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
    frame += char((len >> (8 * len_len)) & 0xFF);
  }

  return frame;
}

uint16_t
WSBuffer::get_closing_code(std::string const &message, std::string *desc)
{
  uint16_t code = 0;
  if (message.size() >= 2) {
    code = static_cast<unsigned char>(message[0]);
    code <<= 8;
    code += static_cast<unsigned char>(message[1]);
    if (desc) {
      *desc = message.substr(2);
    }
  } else {
    if (desc) {
      *desc = "";
    }
  }
  return code;
}
