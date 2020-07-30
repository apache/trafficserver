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
#include "tscore/Allocator.h"
#include "tscore/List.h"
#include "tscore/Ptr.h"
#include "I_IOBuffer.h"
#include <vector>
#include <iterator>
#include <set>

#include "QUICTypes.h"

class QUICFrame;
class QUICStreamFrame;
class QUICCryptoFrame;
class QUICPacketR;
class QUICFrameGenerator;

class QUICFrame
{
public:
  constexpr static int MAX_INSTANCE_SIZE = 256;

  virtual ~QUICFrame() {}
  static QUICFrameType type(const uint8_t *buf);

  QUICFrameId id() const;

  virtual QUICFrameType type() const;
  virtual size_t size() const = 0;
  virtual bool is_probing_frame() const;
  virtual bool is_flow_controlled() const;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const = 0;
  virtual int debug_msg(char *msg, size_t msg_len) const;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet){};
  virtual QUICFrameGenerator *generated_by();
  bool valid() const;
  bool ack_eliciting() const;
  const QUICPacketR *packet() const;
  LINK(QUICFrame, link);

protected:
  virtual void _reset(){};
  QUICFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr, const QUICPacketR *packet = nullptr)
    : _id(id), _owner(owner), _packet(packet)
  {
  }
  size_t _size               = 0;
  bool _valid                = false;
  QUICFrameId _id            = 0;
  QUICFrameGenerator *_owner = nullptr;
  const QUICPacketR *_packet = nullptr;
};

//
// STREAM Frame
//

class QUICStreamFrame : public QUICFrame
{
public:
  QUICStreamFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStreamFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICStreamFrame(Ptr<IOBufferBlock> &block, QUICStreamId streamid, QUICOffset offset, bool last = false,
                  bool has_offset_field = true, bool has_length_field = true, QUICFrameId id = 0,
                  QUICFrameGenerator *owner = nullptr);
  QUICStreamFrame(const QUICStreamFrame &o);

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual bool is_flow_controlled() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

  QUICStreamId stream_id() const;
  QUICOffset offset() const;
  IOBufferBlock *data() const;
  uint64_t data_length() const;
  bool has_offset_field() const;
  bool has_length_field() const;
  bool has_fin_flag() const;

  LINK(QUICStreamFrame, link);

private:
  static constexpr uint8_t MAX_HEADER_SIZE = 32;

  virtual void _reset() override;

  size_t _store_header(uint8_t *buf, size_t *len, bool include_length_field) const;

  Ptr<IOBufferBlock> _block;
  QUICStreamId _stream_id = 0;
  QUICOffset _offset      = 0;
  bool _fin               = false;
  bool _has_offset_field  = true;
  bool _has_length_field  = true;
};

//
// CRYPTO Frame
//

class QUICCryptoFrame : public QUICFrame
{
public:
  QUICCryptoFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICCryptoFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICCryptoFrame(Ptr<IOBufferBlock> &block, QUICOffset offset, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  QUICCryptoFrame(const QUICCryptoFrame &o);

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

  QUICOffset offset() const;
  uint64_t data_length() const;
  IOBufferBlock *data() const;

  LINK(QUICCryptoFrame, link);

private:
  static constexpr uint8_t MAX_HEADER_SIZE = 16;

  virtual void _reset() override;

  size_t _store_header(uint8_t *buf, size_t *len) const;

  QUICOffset _offset = 0;
  Ptr<IOBufferBlock> _block;
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
    AckBlock(uint64_t g, uint64_t l) : _gap(g), _length(l) {}
    uint64_t gap() const;
    uint64_t length() const;
    size_t size() const;
    LINK(QUICAckFrame::AckBlock, link);

  private:
    size_t _get_gap_size() const;
    size_t _get_length_size() const;

    uint64_t _gap    = 0;
    uint64_t _length = 0;
  };

  class AckBlockSection
  {
  public:
    class const_iterator : public std::iterator<std::input_iterator_tag, QUICAckFrame::AckBlock>
    {
    public:
      const_iterator(uint8_t index, const std::vector<QUICAckFrame::AckBlock> *ack_blocks);

      const QUICAckFrame::AckBlock &
      operator*() const
      {
        return this->_current_block;
      };
      const QUICAckFrame::AckBlock *
      operator->() const
      {
        return &this->_current_block;
      };
      const QUICAckFrame::AckBlock &operator++();
      const bool operator!=(const const_iterator &ite) const;
      const bool operator==(const const_iterator &ite) const;

    private:
      uint8_t _index                                         = 0;
      QUICAckFrame::AckBlock _current_block                  = {UINT64_C(0), UINT64_C(0)};
      const std::vector<QUICAckFrame::AckBlock> *_ack_blocks = nullptr;
    };

    AckBlockSection(uint64_t first_ack_block) : _first_ack_block(first_ack_block) {}
    uint8_t count() const;
    size_t size() const;
    Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const;
    uint64_t first_ack_block() const;
    void add_ack_block(const AckBlock &block);
    const_iterator begin() const;
    const_iterator end() const;
    bool has_protected() const;

  private:
    uint64_t _first_ack_block = 0;
    uint8_t _ack_block_count  = 0;
    std::vector<QUICAckFrame::AckBlock> _ack_blocks;
  };

  class EcnSection
  {
  public:
    EcnSection(const uint8_t *buf, size_t len);
    size_t size() const;
    bool valid() const;
    uint64_t ect0_count() const;
    uint64_t ect1_count() const;
    uint64_t ecn_ce_count() const;

  private:
    bool _valid            = false;
    size_t _size           = 0;
    uint64_t _ect0_count   = 0;
    uint64_t _ect1_count   = 0;
    uint64_t _ecn_ce_count = 0;
  };

  QUICAckFrame(QUICFrameId id = 0) : QUICFrame(id) {}
  QUICAckFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICAckFrame(QUICPacketNumber largest_acknowledged, uint64_t ack_delay, uint64_t first_ack_block, QUICFrameId id = 0,
               QUICFrameGenerator *owner = nullptr);
  std::set<PacketNumberRange> ranges() const;

  // There's no reason to restrict copy, but we need to write the copy constructor. Otherwise it will crash on destruct.
  QUICAckFrame(const QUICAckFrame &) = delete;

  virtual ~QUICAckFrame();
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  QUICPacketNumber largest_acknowledged() const;
  uint64_t ack_delay() const;
  uint64_t ack_block_count() const;
  const AckBlockSection *ack_block_section() const;
  AckBlockSection *ack_block_section();
  const EcnSection *ecn_section() const;
  EcnSection *ecn_section();

private:
  virtual void _reset() override;

  QUICPacketNumber _largest_acknowledged = 0;
  uint64_t _ack_delay                    = 0;
  AckBlockSection *_ack_block_section    = nullptr;
  EcnSection *_ecn_section               = nullptr;
};

//
// RESET_STREAM
//

class QUICRstStreamFrame : public QUICFrame
{
public:
  QUICRstStreamFrame(QUICFrameId id = 0) : QUICFrame(id) {}
  QUICRstStreamFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICRstStreamFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset, QUICFrameId id = 0,
                     QUICFrameGenerator *owner = nullptr);

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

  QUICStreamId stream_id() const;
  QUICAppErrorCode error_code() const;
  QUICOffset final_offset() const;

private:
  virtual void _reset() override;

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
  QUICPingFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICPingFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

private:
};

//
// PADDING
//

class QUICPaddingFrame : public QUICFrame
{
public:
  QUICPaddingFrame(size_t size) : _size(size) {}
  QUICPaddingFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual bool is_probing_frame() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

private:
  // padding frame is a resident of padding frames
  // size indicate how many padding frames in this QUICPaddingFrame
  size_t _size = 0;
};

//
// CONNECTION_CLOSE
//

class QUICConnectionCloseFrame : public QUICFrame
{
public:
  QUICConnectionCloseFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICConnectionCloseFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  // Constructor for transport error codes
  QUICConnectionCloseFrame(uint64_t error_code, QUICFrameType frame_type, uint64_t reason_phrase_length, const char *reason_phrase,
                           QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  // Constructor for application protocol error codes
  QUICConnectionCloseFrame(uint64_t error_code, uint64_t reason_phrase_length, const char *reason_phrase, QUICFrameId id = 0,
                           QUICFrameGenerator *owner = nullptr);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

  uint16_t error_code() const;
  QUICFrameType frame_type() const;
  uint64_t reason_phrase_length() const;
  const char *reason_phrase() const;

private:
  virtual void _reset() override;

  uint8_t _type = 0;
  uint64_t _error_code;
  QUICFrameType _frame_type      = QUICFrameType::UNKNOWN;
  uint64_t _reason_phrase_length = 0;
  const char *_reason_phrase     = nullptr;
};

//
// MAX_DATA
//

class QUICMaxDataFrame : public QUICFrame
{
public:
  QUICMaxDataFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICMaxDataFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICMaxDataFrame(uint64_t maximum_data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

  uint64_t maximum_data() const;

private:
  virtual void _reset() override;

  uint64_t _maximum_data = 0;
};

//
// MAX_STREAM_DATA
//

class QUICMaxStreamDataFrame : public QUICFrame
{
public:
  QUICMaxStreamDataFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICMaxStreamDataFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICMaxStreamDataFrame(QUICStreamId stream_id, uint64_t maximum_stream_data, QUICFrameId id = 0,
                         QUICFrameGenerator *owner = nullptr);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  QUICStreamId stream_id() const;
  uint64_t maximum_stream_data() const;

private:
  virtual void _reset() override;

  QUICStreamId _stream_id       = 0;
  uint64_t _maximum_stream_data = 0;
};

//
// MAX_STREAMS
//

class QUICMaxStreamsFrame : public QUICFrame
{
public:
  QUICMaxStreamsFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICMaxStreamsFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICMaxStreamsFrame(QUICStreamId maximum_streams, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  uint64_t maximum_streams() const;

private:
  virtual void _reset() override;

  uint64_t _maximum_streams = 0;
};

//
// BLOCKED
//
class QUICDataBlockedFrame : public QUICFrame
{
public:
  QUICDataBlockedFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICDataBlockedFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICDataBlockedFrame(QUICOffset offset, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _offset(offset){};

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;

  QUICOffset offset() const;

private:
  virtual void _reset() override;

  QUICOffset _offset = 0;
};

//
// STREAM_DATA_BLOCKED
//

class QUICStreamDataBlockedFrame : public QUICFrame
{
public:
  QUICStreamDataBlockedFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStreamDataBlockedFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICStreamDataBlockedFrame(QUICStreamId s, QUICOffset o, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _stream_id(s), _offset(o){};

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  QUICStreamId stream_id() const;
  QUICOffset offset() const;

private:
  virtual void _reset() override;

  QUICStreamId _stream_id = 0;
  QUICOffset _offset      = 0;
};

//
// STREAMS_BLOCKED
//
class QUICStreamIdBlockedFrame : public QUICFrame
{
public:
  QUICStreamIdBlockedFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStreamIdBlockedFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICStreamIdBlockedFrame(QUICStreamId s, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _stream_id(s)
  {
  }
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

  QUICStreamId stream_id() const;

private:
  virtual void _reset() override;

  QUICStreamId _stream_id = 0;
};

//
// NEW_CONNECTION_ID
//

class QUICNewConnectionIdFrame : public QUICFrame
{
public:
  QUICNewConnectionIdFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICNewConnectionIdFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICNewConnectionIdFrame(uint64_t seq, uint64_t ret, const QUICConnectionId &cid, QUICStatelessResetToken token,
                           QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _sequence(seq), _retire_prior_to(ret), _connection_id(cid), _stateless_reset_token(token){};

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  uint64_t sequence() const;
  uint64_t retire_prior_to() const;
  QUICConnectionId connection_id() const;
  QUICStatelessResetToken stateless_reset_token() const;

private:
  virtual void _reset() override;

  uint64_t _sequence              = 0;
  uint64_t _retire_prior_to       = 0;
  QUICConnectionId _connection_id = QUICConnectionId::ZERO();
  QUICStatelessResetToken _stateless_reset_token;
};

//
// STOP_SENDING
//

class QUICStopSendingFrame : public QUICFrame
{
public:
  QUICStopSendingFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStopSendingFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICStopSendingFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICFrameId id = 0,
                       QUICFrameGenerator *owner = nullptr);

  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;

  QUICStreamId stream_id() const;
  QUICAppErrorCode error_code() const;

private:
  virtual void _reset() override;

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
  QUICPathChallengeFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICPathChallengeFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICPathChallengeFrame(ats_unique_buf data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _data(std::move(data))
  {
  }
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual bool is_probing_frame() const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  const uint8_t *data() const;

private:
  virtual void _reset() override;

  ats_unique_buf _data = {nullptr};
};

//
// PATH_RESPONSE
//

class QUICPathResponseFrame : public QUICFrame
{
public:
  static constexpr uint8_t DATA_LEN = 8;
  QUICPathResponseFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICPathResponseFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICPathResponseFrame(ats_unique_buf data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _data(std::move(data))
  {
  }
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual bool is_probing_frame() const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  const uint8_t *data() const;

private:
  virtual void _reset() override;

  ats_unique_buf _data = {nullptr};
};

//
// NEW_TOKEN
//

class QUICNewTokenFrame : public QUICFrame
{
public:
  QUICNewTokenFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICNewTokenFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICNewTokenFrame(ats_unique_buf token, size_t token_length, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _token_length(token_length), _token(std::move(token))
  {
  }
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;

  uint64_t token_length() const;
  const uint8_t *token() const;

private:
  virtual void _reset() override;

  uint64_t _token_length = 0;
  ats_unique_buf _token  = {nullptr};
};

//
// RETIRE_CONNECTION_ID
//

class QUICRetireConnectionIdFrame : public QUICFrame
{
public:
  QUICRetireConnectionIdFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICRetireConnectionIdFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  QUICRetireConnectionIdFrame(uint64_t seq_num, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _seq_num(seq_num)
  {
  }
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  uint64_t seq_num() const;

private:
  virtual void _reset() override;

  uint64_t _seq_num = 0;
};

//
// HANDSHAKE_DONE
//

class QUICHandshakeDoneFrame : public QUICFrame
{
public:
  QUICHandshakeDoneFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICHandshakeDoneFrame(const uint8_t *buf, size_t len, const QUICPacketR *packet = nullptr);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
};

//
// UNKNOWN
//

class QUICUnknownFrame : public QUICFrame
{
public:
  QUICFrameType type() const override;
  size_t size() const override;
  virtual Ptr<IOBufferBlock> to_io_buffer_block(size_t limit) const override;
  void parse(const uint8_t *buf, size_t len, const QUICPacketR *packet) override;
  int debug_msg(char *msg, size_t msg_len) const override;
};

//
// QUICFrameFactory
//
class QUICFrameFactory
{
public:
  /*
   * This is used for creating a QUICFrame object based on received data.
   */
  static QUICFrame *create(uint8_t *buf, const uint8_t *src, size_t len, const QUICPacketR *packet);

  /*
   * This works almost the same as create() but it reuses created objects for performance.
   * If you create a frame object which has the same frame type that you created before, the object will be reset by new data.
   */
  const QUICFrame &fast_create(const uint8_t *buf, size_t len, const QUICPacketR *packet);

  /*
   * Creates a STREAM frame.
   * You have to make sure that the data size won't exceed the maximum size of QUIC packet.
   */
  static QUICStreamFrame *create_stream_frame(uint8_t *buf, Ptr<IOBufferBlock> &block, QUICStreamId stream_id, QUICOffset offset,
                                              bool last = false, bool has_offset_field = true, bool has_length_field = true,
                                              QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a CRYPTO frame.
   * You have to make sure that the data size won't exceed the maximum size of QUIC packet.
   */
  static QUICCryptoFrame *create_crypto_frame(uint8_t *buf, Ptr<IOBufferBlock> &block, QUICOffset offset, QUICFrameId id = 0,
                                              QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a ACK frame.
   * You shouldn't call this directly but through QUICAckFrameCreator because QUICAckFrameCreator manages packet numbers that we
   * need to ack.
   */
  static QUICAckFrame *create_ack_frame(uint8_t *buf, QUICPacketNumber largest_acknowledged, uint64_t ack_delay,
                                        uint64_t first_ack_block, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  /*
   * Creates a CONNECTION_CLOSE frame.
   */
  static QUICConnectionCloseFrame *create_connection_close_frame(uint8_t *buf, uint16_t error_code, QUICFrameType frame_type,
                                                                 uint16_t reason_phrase_length = 0,
                                                                 const char *reason_phrase = nullptr, QUICFrameId id = 0,
                                                                 QUICFrameGenerator *owner = nullptr);

  static QUICConnectionCloseFrame *create_connection_close_frame(uint8_t *buf, const QUICConnectionError &error, QUICFrameId id = 0,
                                                                 QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a MAX_DATA frame.
   */
  static QUICMaxDataFrame *create_max_data_frame(uint8_t *buf, uint64_t maximum_data, QUICFrameId id = 0,
                                                 QUICFrameGenerator *owner = nullptr);

  /*
 /  * Creates a MAX_STREAM_DATA frame.
   */
  static QUICMaxStreamDataFrame *create_max_stream_data_frame(uint8_t *buf, QUICStreamId stream_id, uint64_t maximum_stream_data,
                                                              QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  /*
   * Creates a MAX_STREAMS frame.
   */
  static QUICMaxStreamsFrame *create_max_streams_frame(uint8_t *buf, QUICStreamId maximum_streams, QUICFrameId id = 0,
                                                       QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a PING frame
   */
  static QUICPingFrame *create_ping_frame(uint8_t *buf, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a PATH_CHALLENGE frame
   */
  static QUICPathChallengeFrame *create_path_challenge_frame(uint8_t *buf, const uint8_t *data, QUICFrameId id = 0,
                                                             QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a PATH_RESPONSE frame
   */
  static QUICPathResponseFrame *create_path_response_frame(uint8_t *buf, const uint8_t *data, QUICFrameId id = 0,
                                                           QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a BLOCKED frame.
   */
  static QUICDataBlockedFrame *create_data_blocked_frame(uint8_t *buf, QUICOffset offset, QUICFrameId id = 0,
                                                         QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a STREAM_DATA_BLOCKED frame.
   */
  static QUICStreamDataBlockedFrame *create_stream_data_blocked_frame(uint8_t *buf, QUICStreamId stream_id, QUICOffset offset,
                                                                      QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a STREAMS_BLOCKED frame.
   */
  static QUICStreamIdBlockedFrame *create_stream_id_blocked_frame(uint8_t *buf, QUICStreamId stream_id, QUICFrameId id = 0,
                                                                  QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a RESET_STREAM frame.
   */
  static QUICRstStreamFrame *create_rst_stream_frame(uint8_t *buf, QUICStreamId stream_id, QUICAppErrorCode error_code,
                                                     QUICOffset final_offset, QUICFrameId id = 0,
                                                     QUICFrameGenerator *owner = nullptr);
  static QUICRstStreamFrame *create_rst_stream_frame(uint8_t *buf, QUICStreamError &error, QUICFrameId id = 0,
                                                     QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a STOP_SENDING frame.
   */
  static QUICStopSendingFrame *create_stop_sending_frame(uint8_t *buf, QUICStreamId stream_id, QUICAppErrorCode error_code,
                                                         QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a NEW_CONNECTION_ID frame.
   */
  static QUICNewConnectionIdFrame *create_new_connection_id_frame(uint8_t *buf, uint64_t sequence, uint64_t retire_prior_to,
                                                                  QUICConnectionId connection_id,
                                                                  QUICStatelessResetToken stateless_reset_token, QUICFrameId id = 0,
                                                                  QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a NEW_TOKEN frame
   */
  static QUICNewTokenFrame *create_new_token_frame(uint8_t *buf, const QUICResumptionToken &token, QUICFrameId id = 0,
                                                   QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a RETIRE_CONNECTION_ID frame
   */
  static QUICRetireConnectionIdFrame *create_retire_connection_id_frame(uint8_t *buf, uint64_t seq_num, QUICFrameId id = 0,
                                                                        QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a PADDING frame
   */
  static QUICPaddingFrame *create_padding_frame(uint8_t *buf, size_t size, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a HANDSHAKE_DONE frame
   */
  static QUICHandshakeDoneFrame *create_handshake_done_frame(uint8_t *buf, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

private:
  // FIXME Actual number of frame types is several but some of the values are not sequential.
  QUICFrame *_reusable_frames[256] = {nullptr};
  uint8_t _buf_for_fast_create[256 * QUICFrame::MAX_INSTANCE_SIZE];
  QUICUnknownFrame _unknown_frame;
};
