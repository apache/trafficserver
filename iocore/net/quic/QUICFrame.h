/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <memory>
#include "ts/List.h"
#include <vector>
#include <iterator>

#include "QUICTypes.h"
#include "QUICPacket.h"

class QUICFrame
{
public:
  QUICFrame(const uint8_t *buf, size_t len, bool protection) : _buf(buf), _len(len), _protection(protection) {}
  QUICFrame(bool protection) : _protection(protection) {}
  virtual ~QUICFrame() {}
  static QUICFrameType type(const uint8_t *buf);

  virtual QUICFrameType type() const;
  virtual size_t size() const                                         = 0;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const = 0;
  virtual void reset(const uint8_t *buf, size_t len);
  virtual bool is_protected() const;
  virtual QUICFrame *split(size_t size);
  virtual int debug_msg(char *msg, size_t msg_len) const;
  LINK(QUICFrame, link);

protected:
  QUICFrame() {}
  const uint8_t *_buf = nullptr;
  size_t _len         = 0;
  bool _protection    = true;
};

//
// STREAM Frame
//

class QUICStreamFrame : public QUICFrame
{
public:
  QUICStreamFrame() : QUICFrame() {}
  QUICStreamFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICStreamFrame(ats_unique_buf buf, size_t len, QUICStreamId streamid, QUICOffset offset, bool last = false,
                  bool protection = true);

  QUICFrame *split(size_t size) override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  size_t store(uint8_t *buf, size_t *len, size_t limit, bool include_length_field) const;
  QUICStreamId stream_id() const;
  QUICOffset offset() const;
  const uint8_t *data() const;
  uint64_t data_length() const;
  bool has_offset_field() const;
  bool has_length_field() const;
  bool has_fin_flag() const;

  LINK(QUICStreamFrame, link);

private:
  ats_unique_buf _data    = {nullptr, [](void *p) { ats_free(p); }};
  size_t _data_len        = 0;
  QUICStreamId _stream_id = 0;
  QUICOffset _offset      = 0;
  bool _fin               = false;
  size_t _get_stream_id_field_offset() const;
  size_t _get_offset_field_offset() const;
  size_t _get_length_field_offset() const;
  size_t _get_data_field_offset() const;

  size_t _get_stream_id_field_len() const;
  size_t _get_offset_field_len() const;
  size_t _get_length_field_len() const;
};

//
// ACK Frame
//

class QUICAckFrame : public QUICFrame
{
public:
  class PacketNumberRange
  {
  public:
    PacketNumberRange(QUICPacketNumber first, QUICPacketNumber last) : _first(first), _last(last) {}
    PacketNumberRange(PacketNumberRange &&a) noexcept;
    QUICPacketNumber first() const;
    QUICPacketNumber last() const;
    uint64_t size() const;
    bool contains(QUICPacketNumber x) const;
    bool
    operator<(const PacketNumberRange &b) const
    {
      return static_cast<uint64_t>(this->first()) < static_cast<uint64_t>(b.first());
    }

  private:
    QUICPacketNumber _first;
    QUICPacketNumber _last;
  };

  class AckBlock
  {
  public:
    AckBlock(const uint8_t *b) : _buf(b) {}
    AckBlock(uint64_t g, uint64_t l) : _gap(g), _length(l) {}
    uint64_t gap() const;
    uint64_t length() const;
    size_t size() const;
    const uint8_t *buf() const;

    LINK(QUICAckFrame::AckBlock, link);

  private:
    size_t _get_gap_size() const;
    size_t _get_length_size() const;

    const uint8_t *_buf = nullptr;
    uint64_t _gap       = 0;
    uint64_t _length    = 0;
  };

  class AckBlockSection
  {
  public:
    class const_iterator : public std::iterator<std::input_iterator_tag, QUICAckFrame::AckBlock>
    {
    public:
      const_iterator(uint8_t index, const uint8_t *buf, uint8_t ack_block_count);
      const_iterator(uint8_t index, const std::vector<QUICAckFrame::AckBlock> *ack_blocks);

      const QUICAckFrame::AckBlock &operator*() const { return this->_current_block; };
      const QUICAckFrame::AckBlock *operator->() const { return &this->_current_block; };
      const QUICAckFrame::AckBlock &operator++();
      const bool operator!=(const const_iterator &ite) const;
      const bool operator==(const const_iterator &ite) const;

    private:
      uint8_t _index                                         = 0;
      const uint8_t *_buf                                    = nullptr;
      QUICAckFrame::AckBlock _current_block                  = {UINT64_C(0), UINT64_C(0)};
      const std::vector<QUICAckFrame::AckBlock> *_ack_blocks = nullptr;
    };

    AckBlockSection(uint64_t first_ack_block) : _first_ack_block(first_ack_block) {}
    AckBlockSection(const uint8_t *buf, uint8_t ack_block_count) : _buf(buf), _ack_block_count(ack_block_count) {}
    uint8_t count() const;
    size_t size() const;
    size_t store(uint8_t *buf, size_t *len, size_t limit) const;
    uint64_t first_ack_block() const;
    void add_ack_block(const AckBlock block, bool protection = true);
    const_iterator begin() const;
    const_iterator end() const;
    bool has_protected() const;

  private:
    size_t _get_first_ack_block_size() const;

    const uint8_t *_buf       = nullptr;
    uint64_t _first_ack_block = 0;
    uint8_t _ack_block_count  = 0;
    std::vector<QUICAckFrame::AckBlock> _ack_blocks;
    bool _protection = false;
  };

  QUICAckFrame() : QUICFrame() {}
  QUICAckFrame(const uint8_t *buf, size_t len, bool protection = true);
  QUICAckFrame(QUICPacketNumber largest_acknowledged, uint64_t ack_delay, uint64_t first_ack_block, bool protection = true);

  virtual ~QUICAckFrame();
  virtual void reset(const uint8_t *buf, size_t len) override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  bool is_protected() const override;

  QUICPacketNumber largest_acknowledged() const;
  uint64_t ack_delay() const;
  uint64_t ack_block_count() const;
  const AckBlockSection *ack_block_section() const;
  AckBlockSection *ack_block_section();

private:
  size_t _get_largest_acknowledged_offset() const;
  size_t _get_largest_acknowledged_length() const;
  size_t _get_ack_delay_offset() const;
  size_t _get_ack_delay_length() const;
  size_t _get_ack_block_count_offset() const;
  size_t _get_ack_block_count_length() const;
  size_t _get_ack_block_section_offset() const;

  QUICPacketNumber _largest_acknowledged = 0;
  uint64_t _ack_delay                    = 0;
  AckBlockSection *_ack_block_section    = nullptr;
};

//
// RST_STREAM
//

class QUICRstStreamFrame : public QUICFrame
{
public:
  QUICRstStreamFrame() : QUICFrame() {}
  QUICRstStreamFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICRstStreamFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset, bool protection = true);

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  QUICStreamId stream_id() const;
  QUICAppErrorCode error_code() const;
  QUICOffset final_offset() const;

private:
  size_t _get_stream_id_field_offset() const;
  size_t _get_stream_id_field_length() const;
  size_t _get_error_code_field_offset() const;
  size_t _get_final_offset_field_offset() const;
  size_t _get_final_offset_field_length() const;

  QUICStreamId _stream_id      = 0;
  QUICAppErrorCode _error_code = 0;
  QUICOffset _final_offset     = 0;
};

//
// PING
//

class QUICPingFrame : public QUICFrame
{
public:
  QUICPingFrame() : QUICFrame() {}
  QUICPingFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICPingFrame(bool protection) : QUICFrame(protection) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

private:
};

// PADDING

class QUICPaddingFrame : public QUICFrame
{
public:
  QUICPaddingFrame() : QUICFrame() {}
  QUICPaddingFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
};

//
// CONNECTION_CLOSE
//

class QUICConnectionCloseFrame : public QUICFrame
{
public:
  QUICConnectionCloseFrame() : QUICFrame() {}
  QUICConnectionCloseFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICConnectionCloseFrame(QUICTransErrorCode error_code, uint64_t reason_phrase_length, const char *reason_phrase,
                           bool protection = true);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  QUICTransErrorCode error_code() const;
  uint64_t reason_phrase_length() const;
  const char *reason_phrase() const;

private:
  size_t _get_reason_phrase_length_field_offset() const;
  size_t _get_reason_phrase_length_field_length() const;
  size_t _get_reason_phrase_field_offset() const;

  QUICTransErrorCode _error_code;
  uint64_t _reason_phrase_length = 0;
  const char *_reason_phrase     = nullptr;
};

//
// APPLICATION_CLOSE
//

class QUICApplicationCloseFrame : public QUICFrame
{
public:
  QUICApplicationCloseFrame() : QUICFrame() {}
  QUICApplicationCloseFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICApplicationCloseFrame(QUICAppErrorCode error_code, uint64_t reason_phrase_length, const char *reason_phrase,
                            bool protection = true);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  QUICAppErrorCode error_code() const;
  uint64_t reason_phrase_length() const;
  const char *reason_phrase() const;

private:
  size_t _get_reason_phrase_length_field_offset() const;
  size_t _get_reason_phrase_length_field_length() const;
  size_t _get_reason_phrase_field_offset() const;

  QUICAppErrorCode _error_code   = 0;
  uint64_t _reason_phrase_length = 0;
  const char *_reason_phrase     = nullptr;
};

//
// MAX_DATA
//

class QUICMaxDataFrame : public QUICFrame
{
public:
  QUICMaxDataFrame() : QUICFrame() {}
  QUICMaxDataFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICMaxDataFrame(uint64_t maximum_data, bool protection = true);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  uint64_t maximum_data() const;

private:
  size_t _get_max_data_field_length() const;

  uint64_t _maximum_data = 0;
};

//
// MAX_STREAM_DATA
//

class QUICMaxStreamDataFrame : public QUICFrame
{
public:
  QUICMaxStreamDataFrame() : QUICFrame() {}
  QUICMaxStreamDataFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICMaxStreamDataFrame(QUICStreamId stream_id, uint64_t maximum_stream_data, bool protection = true);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  QUICStreamId stream_id() const;
  uint64_t maximum_stream_data() const;

private:
  size_t _get_stream_id_field_offset() const;
  size_t _get_stream_id_field_length() const;
  size_t _get_max_stream_data_field_offset() const;
  size_t _get_max_stream_data_field_length() const;

  QUICStreamId _stream_id       = 0;
  uint64_t _maximum_stream_data = 0;
};

//
// MAX_STREAM_ID
//

class QUICMaxStreamIdFrame : public QUICFrame
{
public:
  QUICMaxStreamIdFrame() : QUICFrame() {}
  QUICMaxStreamIdFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICMaxStreamIdFrame(QUICStreamId maximum_stream_id, bool protection = true);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  QUICStreamId maximum_stream_id() const;

private:
  size_t _get_max_stream_id_field_length() const;

  QUICStreamId _maximum_stream_id = 0;
};

//
// BLOCKED
//
class QUICBlockedFrame : public QUICFrame
{
public:
  QUICBlockedFrame() : QUICFrame() {}
  QUICBlockedFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICBlockedFrame(QUICOffset offset, bool protection = true) : QUICFrame(protection), _offset(offset){};

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  QUICOffset offset() const;

private:
  size_t _get_offset_field_length() const;

  QUICOffset _offset = 0;
};

//
// STREAM_BLOCKED
//

class QUICStreamBlockedFrame : public QUICFrame
{
public:
  QUICStreamBlockedFrame() : QUICFrame() {}
  QUICStreamBlockedFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICStreamBlockedFrame(QUICStreamId s, QUICOffset o, bool protection = true) : QUICFrame(protection), _stream_id(s), _offset(o){};

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  QUICStreamId stream_id() const;
  QUICOffset offset() const;

private:
  size_t _get_stream_id_field_length() const;
  size_t _get_offset_field_offset() const;
  size_t _get_offset_field_length() const;

  QUICStreamId _stream_id = 0;
  QUICOffset _offset      = 0;
};

//
// STREAM_ID_BLOCKED
//
class QUICStreamIdBlockedFrame : public QUICFrame
{
public:
  QUICStreamIdBlockedFrame() : QUICFrame() {}
  QUICStreamIdBlockedFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICStreamIdBlockedFrame(QUICStreamId s, bool protection = true) : QUICFrame(protection), _stream_id(s) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  QUICStreamId stream_id() const;

private:
  size_t _get_stream_id_field_length() const;

  QUICStreamId _stream_id = 0;
};

//
// NEW_CONNECTION_ID
//

class QUICNewConnectionIdFrame : public QUICFrame
{
public:
  QUICNewConnectionIdFrame() : QUICFrame() {}
  QUICNewConnectionIdFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICNewConnectionIdFrame(uint64_t seq, QUICConnectionId id, QUICStatelessResetToken token, bool protection = true)
    : QUICFrame(protection), _sequence(seq), _connection_id(id), _stateless_reset_token(token){};

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  uint64_t sequence() const;
  QUICConnectionId connection_id() const;
  QUICStatelessResetToken stateless_reset_token() const;

private:
  size_t _get_sequence_field_length() const;
  size_t _get_connection_id_field_offset() const;
  size_t _get_connection_id_length() const;

  uint64_t _sequence              = 0;
  QUICConnectionId _connection_id = QUICConnectionId::ZERO();
  QUICStatelessResetToken _stateless_reset_token;
};

//
// STOP_SENDING
//

class QUICStopSendingFrame : public QUICFrame
{
public:
  QUICStopSendingFrame() : QUICFrame() {}
  QUICStopSendingFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICStopSendingFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, bool protection = true);

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  QUICStreamId stream_id() const;
  QUICAppErrorCode error_code() const;

private:
  size_t _get_stream_id_field_length() const;
  size_t _get_error_code_field_offset() const;

  QUICStreamId _stream_id      = 0;
  QUICAppErrorCode _error_code = 0;
};

//
// PATH_CHALLENGE
//

class QUICPathChallengeFrame : public QUICFrame
{
public:
  static constexpr uint8_t DATA_LEN = 8;
  QUICPathChallengeFrame() : QUICFrame() {}
  QUICPathChallengeFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICPathChallengeFrame(ats_unique_buf data, bool protection = true) : QUICFrame(protection), _data(std::move(data)) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  const uint8_t *data() const;

private:
  const size_t _data_offset() const;

  ats_unique_buf _data = {nullptr, [](void *p) { ats_free(p); }};
};

//
// PATH_RESPONSE
//

class QUICPathResponseFrame : public QUICFrame
{
public:
  static constexpr uint8_t DATA_LEN = 8;
  QUICPathResponseFrame() : QUICFrame() {}
  QUICPathResponseFrame(const uint8_t *buf, size_t len, bool protection = true) : QUICFrame(buf, len, protection) {}
  QUICPathResponseFrame(ats_unique_buf data, bool protection = true) : QUICFrame(protection), _data(std::move(data)) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  const uint8_t *data() const;

private:
  const size_t _data_offset() const;

  ats_unique_buf _data = {nullptr, [](void *p) { ats_free(p); }};
};

using QUICFrameDeleterFunc = void (*)(QUICFrame *p);
using QUICFrameUPtr        = std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>;
using QUICStreamFrameUPtr  = std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc>;

//
// Retransmission Frame
//

class QUICRetransmissionFrame : public QUICFrame
{
public:
  QUICRetransmissionFrame() : QUICFrame() {}
  QUICRetransmissionFrame(QUICFrameUPtr original_frame, const QUICPacket &original_packet);
  QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  QUICPacketType packet_type() const;
  QUICFrame *split(size_t size) override;

private:
  QUICFrameUPtr _frame = QUICFrameUPtr(nullptr, nullptr);
  ats_unique_buf _data = ats_unique_buf(nullptr, [](void *p) { ats_free(p); });
  QUICPacketType _packet_type;
};

extern ClassAllocator<QUICStreamFrame> quicStreamFrameAllocator;
extern ClassAllocator<QUICAckFrame> quicAckFrameAllocator;
extern ClassAllocator<QUICPaddingFrame> quicPaddingFrameAllocator;
extern ClassAllocator<QUICRstStreamFrame> quicRstStreamFrameAllocator;
extern ClassAllocator<QUICConnectionCloseFrame> quicConnectionCloseFrameAllocator;
extern ClassAllocator<QUICApplicationCloseFrame> quicApplicationCloseFrameAllocator;
extern ClassAllocator<QUICMaxDataFrame> quicMaxDataFrameAllocator;
extern ClassAllocator<QUICMaxStreamDataFrame> quicMaxStreamDataFrameAllocator;
extern ClassAllocator<QUICMaxStreamIdFrame> quicMaxStreamIdFrameAllocator;
extern ClassAllocator<QUICPingFrame> quicPingFrameAllocator;
extern ClassAllocator<QUICBlockedFrame> quicBlockedFrameAllocator;
extern ClassAllocator<QUICStreamBlockedFrame> quicStreamBlockedFrameAllocator;
extern ClassAllocator<QUICStreamIdBlockedFrame> quicStreamIdBlockedFrameAllocator;
extern ClassAllocator<QUICNewConnectionIdFrame> quicNewConnectionIdFrameAllocator;
extern ClassAllocator<QUICStopSendingFrame> quicStopSendingFrameAllocator;
extern ClassAllocator<QUICPathChallengeFrame> quicPathChallengeFrameAllocator;
extern ClassAllocator<QUICPathResponseFrame> quicPathResponseFrameAllocator;
extern ClassAllocator<QUICRetransmissionFrame> quicRetransmissionFrameAllocator;

class QUICFrameDeleter
{
public:
  // TODO Probably these methods should call destructor
  static void
  delete_null_frame(QUICFrame *frame)
  {
    ink_assert(frame == nullptr);
  }

  static void
  delete_stream_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicStreamFrameAllocator.free(static_cast<QUICStreamFrame *>(frame));
  }

  static void
  delete_ack_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicAckFrameAllocator.free(static_cast<QUICAckFrame *>(frame));
  }

  static void
  delete_padding_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicPaddingFrameAllocator.free(static_cast<QUICPaddingFrame *>(frame));
  }

  static void
  delete_rst_stream_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicRstStreamFrameAllocator.free(static_cast<QUICRstStreamFrame *>(frame));
  }

  static void
  delete_connection_close_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicConnectionCloseFrameAllocator.free(static_cast<QUICConnectionCloseFrame *>(frame));
  }

  static void
  delete_application_close_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicApplicationCloseFrameAllocator.free(static_cast<QUICApplicationCloseFrame *>(frame));
  }

  static void
  delete_max_data_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicMaxDataFrameAllocator.free(static_cast<QUICMaxDataFrame *>(frame));
  }

  static void
  delete_max_stream_data_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicMaxStreamDataFrameAllocator.free(static_cast<QUICMaxStreamDataFrame *>(frame));
  }

  static void
  delete_max_stream_id_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicMaxStreamIdFrameAllocator.free(static_cast<QUICMaxStreamIdFrame *>(frame));
  }

  static void
  delete_ping_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicPingFrameAllocator.free(static_cast<QUICPingFrame *>(frame));
  }

  static void
  delete_blocked_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicBlockedFrameAllocator.free(static_cast<QUICBlockedFrame *>(frame));
  }

  static void
  delete_stream_blocked_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicStreamBlockedFrameAllocator.free(static_cast<QUICStreamBlockedFrame *>(frame));
  }

  static void
  delete_stream_id_blocked_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicStreamIdBlockedFrameAllocator.free(static_cast<QUICStreamIdBlockedFrame *>(frame));
  }

  static void
  delete_new_connection_id_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicNewConnectionIdFrameAllocator.free(static_cast<QUICNewConnectionIdFrame *>(frame));
  }

  static void
  delete_stop_sending_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicStopSendingFrameAllocator.free(static_cast<QUICStopSendingFrame *>(frame));
  }

  static void
  delete_path_challenge_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicPathChallengeFrameAllocator.free(static_cast<QUICPathChallengeFrame *>(frame));
  }

  static void
  delete_path_response_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicPathResponseFrameAllocator.free(static_cast<QUICPathResponseFrame *>(frame));
  }

  static void
  delete_retransmission_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicRetransmissionFrameAllocator.free(static_cast<QUICRetransmissionFrame *>(frame));
  }
};

//
// QUICFrameFactory
//
class QUICFrameFactory
{
public:
  /*
   * Split Stream frame into two frame
   * Return the new frame
   */
  static QUICFrameUPtr split_frame(QUICFrame *frame, size_t size);

  /*
   * This is for an empty QUICFrameUptr.
   * Empty frames are used for variable initialization and return value of frame creation failure
   */
  static QUICFrameUPtr create_null_frame();
  static std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create_null_ack_frame();

  /*
   * This is used for creating a QUICFrame object based on received data.
   */
  static QUICFrameUPtr create(const uint8_t *buf, size_t len);

  /*
   * This works almost the same as create() but it reuses created objects for performance.
   * If you create a frame object which has the same frame type that you created before, the object will be reset by new data.
   */
  std::shared_ptr<const QUICFrame> fast_create(const uint8_t *buf, size_t len);

  /*
   * Creates a STREAM frame.
   * You have to make sure that the data size won't exceed the maximum size of QUIC packet.
   */
  static QUICStreamFrameUPtr create_stream_frame(const uint8_t *data, size_t data_len, QUICStreamId stream_id, QUICOffset offset,
                                                 bool last = false, bool protection = true);
  /*
   * Creates a ACK frame.
   * You shouldn't call this directly but through QUICAckFrameCreator because QUICAckFrameCreator manages packet numbers that we
   * need to ack.
   */
  static std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create_ack_frame(QUICPacketNumber largest_acknowledged,
                                                                              uint64_t ack_delay, uint64_t first_ack_block,
                                                                              bool protection = true);
  /*
   * Creates a CONNECTION_CLOSE frame.
   */
  static std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> create_connection_close_frame(
    QUICTransErrorCode error_code, uint16_t reason_phrase_length = 0, const char *reason_phrase = nullptr);
  static std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> create_connection_close_frame(
    QUICConnectionErrorUPtr error);

  /*
   * Creates a APPLICATION_CLOSE frame.
   */
  static std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc> create_application_close_frame(
    QUICAppErrorCode error_code, uint16_t reason_phrase_length = 0, const char *reason_phrase = nullptr);
  static std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc> create_application_close_frame(
    QUICConnectionErrorUPtr error);

  /*
   * Creates a MAX_DATA frame.
   */
  static std::unique_ptr<QUICMaxDataFrame, QUICFrameDeleterFunc> create_max_data_frame(uint64_t maximum_data);

  /*
   * Creates a MAX_STREAM_DATA frame.
   */
  static std::unique_ptr<QUICMaxStreamDataFrame, QUICFrameDeleterFunc> create_max_stream_data_frame(QUICStreamId stream_id,
                                                                                                    uint64_t maximum_stream_data);

  /*
   * Creates a PING frame
   */
  static std::unique_ptr<QUICPingFrame, QUICFrameDeleterFunc> create_ping_frame();

  /*
   * Creates a PATH_CHALLENGE frame
   */
  static std::unique_ptr<QUICPathChallengeFrame, QUICFrameDeleterFunc> create_path_challenge_frame(const uint8_t *data);

  /*
   * Creates a PATH_RESPONSE frame
   */
  static std::unique_ptr<QUICPathResponseFrame, QUICFrameDeleterFunc> create_path_response_frame(const uint8_t *data);

  /*
   * Creates a BLOCKED frame.
   */
  static std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc> create_blocked_frame(QUICOffset offset);

  /*
   * Creates a STREAM_BLOCKED frame.
   */
  static std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc> create_stream_blocked_frame(QUICStreamId stream_id,
                                                                                                   QUICOffset offset);

  /*
   * Creates a STREAM_ID_BLOCKED frame.
   */
  static std::unique_ptr<QUICStreamIdBlockedFrame, QUICFrameDeleterFunc> create_stream_id_blocked_frame(QUICStreamId stream_id);

  /*
   * Creates a RST_STREAM frame.
   */
  static std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> create_rst_stream_frame(QUICStreamId stream_id,
                                                                                           QUICAppErrorCode error_code,
                                                                                           QUICOffset final_offset);
  static std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> create_rst_stream_frame(QUICStreamErrorUPtr error);

  /*
   * Creates a STOP_SENDING frame.
   */
  static std::unique_ptr<QUICStopSendingFrame, QUICFrameDeleterFunc> create_stop_sending_frame(QUICStreamId stream_id,
                                                                                               QUICAppErrorCode error_code);

  /*
   * Creates a NEW_CONNECTION_ID frame.
   */
  static std::unique_ptr<QUICNewConnectionIdFrame, QUICFrameDeleterFunc> create_new_connection_id_frame(
    uint32_t sequence, QUICConnectionId connectoin_id, QUICStatelessResetToken stateless_reset_token);

  /*
   * Creates a retransmission frame, which is very special.
   * This retransmission frame will be used only for retransmission and it's not a standard frame type.
   */
  static std::unique_ptr<QUICRetransmissionFrame, QUICFrameDeleterFunc> create_retransmission_frame(
    QUICFrameUPtr original_frame, const QUICPacket &original_packet);

private:
  // FIXME Actual number of frame types is several but some of the values are not sequential.
  std::shared_ptr<QUICFrame> _reusable_frames[256] = {nullptr};
};
