#pragma once

#include "QUICPacket.h"
#include "QUICFrame.h"

#include <type_traits>

namespace QUICDetail
{
template <typename Base, typename Derived> struct is_derived_from {
  typedef typename std::decay<Derived>::type raw_type;                 // remove reference or const reference
  typedef std::is_base_of<Base, raw_type> base_of;                     // check whether it is based of `Base`
  typedef typename std::enable_if<base_of::value, Derived>::type type; // enable type if base_of check is true
};

} // namespace QUICDetail

// FIXME remove this
class QUICFrameReaderUnbond
{
public:
  QUICFrameReaderUnbond(const QUICPacket &p) : _length(p.payload_length())
  {
    this->_payload    = ats_unique_malloc(p.payload_length());
    size_t copied_len = 0;
    for (auto b = p.payload_block(); b; b = b->next) {
      memcpy(this->_payload.get() + copied_len, b->start(), b->size());
      copied_len += b->size();
    }
  }
  // FIXME remove bind_packet;
  const QUICFrame *
  read_frame(uint8_t *buf)
  {
    if (this->_cursor >= this->_length) {
      return nullptr;
    }

    auto frame = QUICFrameFactory::create(buf, this->_payload.get() + this->_cursor, this->_length - this->_cursor, nullptr);
    this->_cursor += frame->size();
    return frame;
  }

private:
  const size_t _length = 0;
  ats_unique_buf _payload;
  uint16_t _cursor = 0;
};

// this template is transform QUICPacket to QUICFrames
template <typename P,
          typename QUICDetail::is_derived_from<QUICPacket, P>::type * = nullptr> // checking whether P is validate object
class QUICFrameReader
{
public:
  QUICFrameReader(P &p) : _packet(p)
  {
    this->_payload    = ats_unique_malloc(p.payload_length());
    size_t copied_len = 0;
    for (auto b = p.payload_block(); b; b = b->next) {
      memcpy(this->_payload.get() + copied_len, b->start(), b->size());
      copied_len += b->size();
    }
  }

  // read frame one by one
  const QUICFrame *
  read_frame(QUICFrameFactory &factroy)
  {
    if (this->_cursor >= this->_packet.payload_length()) {
      return nullptr;
    }

    // TODO only QUICPathValidator need the packet information. And only address is used
    // it should be a way to remove the packet binding. so we can merge QUICFrameReader and QUICFrameReaderUnbond.
    auto &frame =
      factroy.fast_create(this->_payload.get() + this->_cursor, this->_packet.payload_length() - this->_cursor, &this->_packet);
    this->_cursor += frame.size();
    return &frame;
  }

  // FIXME remove bind_packet;
  const QUICFrame *
  read_frame(uint8_t *buf)
  {
    if (this->_cursor >= this->_packet.payload_length()) {
      return nullptr;
    }

    // TODO only QUICPathValidator need the packet information. And only address is used
    // it should be a way to remove the packet binding. so we can merge QUICFrameReader and QUICFrameReaderUnbond.
    auto frame = QUICFrameFactory::create(buf, this->_payload.get() + this->_cursor, this->_packet.payload_length() - this->_cursor,
                                          &this->_packet);
    this->_cursor += frame->size();
    return frame;
  }

private:
  ats_unique_buf _payload;
  uint16_t _cursor = 0;
  P &_packet;
};