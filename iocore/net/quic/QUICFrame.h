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

#include "QUICTypes.h"

class QUICFrame;
class QUICStreamFrame;
class QUICCryptoFrame;
class QUICPacket;
class QUICFrameGenerator;

using QUICFrameDeleterFunc = void (*)(QUICFrame *p);
using QUICFrameUPtr        = std::unique_ptr<QUICFrame, QUICFrameDeleterFunc>;
using QUICStreamFrameUPtr  = std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc>;
using QUICCryptoFrameUPtr  = std::unique_ptr<QUICCryptoFrame, QUICFrameDeleterFunc>;

using QUICFrameSPtr       = std::shared_ptr<const QUICFrame>;
using QUICStreamFrameSPtr = std::shared_ptr<const QUICStreamFrame>;
using QUICCryptoFrameSPtr = std::shared_ptr<const QUICCryptoFrame>;

using QUICFrameId = uint64_t;

class QUICFrame
{
public:
  virtual ~QUICFrame() {}
  static QUICFrameType type(const uint8_t *buf);

  QUICFrameId id() const;

  virtual QUICFrameUPtr clone() const = 0;
  virtual QUICFrameType type() const;
  virtual size_t size() const = 0;
  virtual bool is_probing_frame() const;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const = 0;
  virtual QUICFrame *split(size_t size);
  virtual int debug_msg(char *msg, size_t msg_len) const;
  virtual void parse(const uint8_t *buf, size_t len){};
  virtual QUICFrameGenerator *generated_by();
  bool valid() const;
  LINK(QUICFrame, link);

protected:
  virtual void _reset(){};
  QUICFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : _id(id), _owner(owner) {}
  size_t _size               = 0;
  bool _valid                = false;
  QUICFrameId _id            = 0;
  QUICFrameGenerator *_owner = nullptr;
};

//
// STREAM Frame
//

class QUICStreamFrame : public QUICFrame
{
public:
  QUICStreamFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStreamFrame(const uint8_t *buf, size_t len);
  QUICStreamFrame(Ptr<IOBufferBlock> &block, QUICStreamId streamid, QUICOffset offset, bool last = false,
                  bool has_offset_field = true, bool has_length_field = true, QUICFrameId id = 0,
                  QUICFrameGenerator *owner = nullptr);

  QUICFrame *split(size_t size) override;
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

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
  virtual void _reset() override;

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
  QUICCryptoFrame(const uint8_t *buf, size_t len);
  QUICCryptoFrame(Ptr<IOBufferBlock> &block, QUICOffset offset, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  QUICFrame *split(size_t size) override;
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

  QUICOffset offset() const;
  uint64_t data_length() const;
  const uint8_t *data() const;

  LINK(QUICCryptoFrame, link);

private:
  virtual void _reset() override;

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

      const QUICAckFrame::AckBlock &operator*() const { return this->_current_block; };
      const QUICAckFrame::AckBlock *operator->() const { return &this->_current_block; };
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
    size_t store(uint8_t *buf, size_t *len, size_t limit) const;
    uint64_t first_ack_block() const;
    void add_ack_block(const AckBlock block);
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
  QUICAckFrame(const uint8_t *buf, size_t len);
  QUICAckFrame(QUICPacketNumber largest_acknowledged, uint64_t ack_delay, uint64_t first_ack_block, QUICFrameId id = 0,
               QUICFrameGenerator *owner = nullptr);

  virtual ~QUICAckFrame();
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
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
// RST_STREAM
//

class QUICRstStreamFrame : public QUICFrame
{
public:
  QUICRstStreamFrame(QUICFrameId id = 0) : QUICFrame(id) {}
  QUICRstStreamFrame(const uint8_t *buf, size_t len);
  QUICRstStreamFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICOffset final_offset, QUICFrameId id = 0,
                     QUICFrameGenerator *owner = nullptr);

  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

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
  QUICPingFrame(const uint8_t *buf, size_t len);
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

private:
};

//
// PADDING
//

class QUICPaddingFrame : public QUICFrame
{
public:
  QUICPaddingFrame() {}
  QUICPaddingFrame(const uint8_t *buf, size_t len);
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual bool is_probing_frame() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
};

//
// CONNECTION_CLOSE
//

class QUICConnectionCloseFrame : public QUICFrame
{
public:
  QUICConnectionCloseFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICConnectionCloseFrame(const uint8_t *buf, size_t len);
  QUICConnectionCloseFrame(uint16_t error_code, QUICFrameType frame_type, uint64_t reason_phrase_length, const char *reason_phrase,
                           QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

  uint16_t error_code() const;
  QUICFrameType frame_type() const;
  uint64_t reason_phrase_length() const;
  const char *reason_phrase() const;

private:
  virtual void _reset() override;

  uint16_t _error_code;
  QUICFrameType _frame_type      = QUICFrameType::UNKNOWN;
  uint64_t _reason_phrase_length = 0;
  const char *_reason_phrase     = nullptr;
};

//
// APPLICATION_CLOSE
//

class QUICApplicationCloseFrame : public QUICFrame
{
public:
  QUICApplicationCloseFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICApplicationCloseFrame(const uint8_t *buf, size_t len);
  QUICApplicationCloseFrame(QUICAppErrorCode error_code, uint64_t reason_phrase_length, const char *reason_phrase,
                            QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  QUICFrameUPtr clone() const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

  QUICAppErrorCode error_code() const;
  uint64_t reason_phrase_length() const;
  const char *reason_phrase() const;

private:
  virtual void _reset() override;

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
  QUICMaxDataFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICMaxDataFrame(const uint8_t *buf, size_t len);
  QUICMaxDataFrame(uint64_t maximum_data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

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
  QUICMaxStreamDataFrame(const uint8_t *buf, size_t len);
  QUICMaxStreamDataFrame(QUICStreamId stream_id, uint64_t maximum_stream_data, QUICFrameId id = 0,
                         QUICFrameGenerator *owner = nullptr);
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  QUICStreamId stream_id() const;
  uint64_t maximum_stream_data() const;

private:
  virtual void _reset() override;

  QUICStreamId _stream_id       = 0;
  uint64_t _maximum_stream_data = 0;
};

//
// MAX_STREAM_ID
//

class QUICMaxStreamIdFrame : public QUICFrame
{
public:
  QUICMaxStreamIdFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICMaxStreamIdFrame(const uint8_t *buf, size_t len);
  QUICMaxStreamIdFrame(QUICStreamId maximum_stream_id, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  QUICStreamId maximum_stream_id() const;

private:
  virtual void _reset() override;

  QUICStreamId _maximum_stream_id = 0;
};

//
// BLOCKED
//
class QUICBlockedFrame : public QUICFrame
{
public:
  QUICBlockedFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICBlockedFrame(const uint8_t *buf, size_t len);
  QUICBlockedFrame(QUICOffset offset, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _offset(offset){};

  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

  QUICOffset offset() const;

private:
  virtual void _reset() override;

  QUICOffset _offset = 0;
};

//
// STREAM_BLOCKED
//

class QUICStreamBlockedFrame : public QUICFrame
{
public:
  QUICStreamBlockedFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStreamBlockedFrame(const uint8_t *buf, size_t len);
  QUICStreamBlockedFrame(QUICStreamId s, QUICOffset o, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _stream_id(s), _offset(o){};

  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

  QUICStreamId stream_id() const;
  QUICOffset offset() const;

private:
  virtual void _reset() override;

  QUICStreamId _stream_id = 0;
  QUICOffset _offset      = 0;
};

//
// STREAM_ID_BLOCKED
//
class QUICStreamIdBlockedFrame : public QUICFrame
{
public:
  QUICStreamIdBlockedFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStreamIdBlockedFrame(const uint8_t *buf, size_t len);
  QUICStreamIdBlockedFrame(QUICStreamId s, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _stream_id(s)
  {
  }
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

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
  QUICNewConnectionIdFrame(const uint8_t *buf, size_t len);
  QUICNewConnectionIdFrame(uint64_t seq, QUICConnectionId cid, QUICStatelessResetToken token, QUICFrameId id = 0,
                           QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _sequence(seq), _connection_id(cid), _stateless_reset_token(token){};

  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  uint64_t sequence() const;
  QUICConnectionId connection_id() const;
  QUICStatelessResetToken stateless_reset_token() const;

private:
  virtual void _reset() override;

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
  QUICStopSendingFrame(QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr) : QUICFrame(id, owner) {}
  QUICStopSendingFrame(const uint8_t *buf, size_t len);
  QUICStopSendingFrame(QUICStreamId stream_id, QUICAppErrorCode error_code, QUICFrameId id = 0,
                       QUICFrameGenerator *owner = nullptr);

  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

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
  QUICPathChallengeFrame(const uint8_t *buf, size_t len);
  QUICPathChallengeFrame(ats_unique_buf data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _data(std::move(data))
  {
  }
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual bool is_probing_frame() const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

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
  QUICPathResponseFrame(const uint8_t *buf, size_t len);
  QUICPathResponseFrame(ats_unique_buf data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _data(std::move(data))
  {
  }
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual bool is_probing_frame() const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;

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
  QUICNewTokenFrame(const uint8_t *buf, size_t len);
  QUICNewTokenFrame(ats_unique_buf token, size_t token_length, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _token_length(token_length), _token(std::move(token))
  {
  }
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;

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
  QUICRetireConnectionIdFrame(const uint8_t *buf, size_t len);
  QUICRetireConnectionIdFrame(uint64_t seq_num, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr)
    : QUICFrame(id, owner), _seq_num(seq_num)
  {
  }
  QUICFrameUPtr clone() const override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual void parse(const uint8_t *buf, size_t len) override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;

  uint64_t seq_num() const;

private:
  virtual void _reset() override;

  uint64_t _seq_num = 0;
};

//
// Retransmission Frame - Not on the spec
//

class QUICRetransmissionFrame : public QUICFrame
{
public:
  QUICRetransmissionFrame() {}
  QUICRetransmissionFrame(QUICFrameUPtr original_frame, const QUICPacket &original_packet);
  QUICFrameUPtr clone() const override;
  QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual size_t store(uint8_t *buf, size_t *len, size_t limit) const override;
  virtual int debug_msg(char *msg, size_t msg_len) const override;
  QUICPacketType packet_type() const;
  QUICFrame *split(size_t size) override;

private:
  QUICFrameUPtr _frame = QUICFrameUPtr(nullptr, nullptr);
  ats_unique_buf _data = ats_unique_buf(nullptr);
  QUICPacketType _packet_type;
};

extern ClassAllocator<QUICStreamFrame> quicStreamFrameAllocator;
extern ClassAllocator<QUICCryptoFrame> quicCryptoFrameAllocator;
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
extern ClassAllocator<QUICNewTokenFrame> quicNewTokenFrameAllocator;
extern ClassAllocator<QUICRetireConnectionIdFrame> quicRetireConnectionIdFrameAllocator;
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
  delete_crypto_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicCryptoFrameAllocator.free(static_cast<QUICCryptoFrame *>(frame));
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
  delete_new_token_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicNewTokenFrameAllocator.free(static_cast<QUICNewTokenFrame *>(frame));
  }

  static void
  delete_retire_connection_id_frame(QUICFrame *frame)
  {
    frame->~QUICFrame();
    quicRetireConnectionIdFrameAllocator.free(static_cast<QUICRetireConnectionIdFrame *>(frame));
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
  static QUICStreamFrameUPtr create_stream_frame(Ptr<IOBufferBlock> &block, QUICStreamId stream_id, QUICOffset offset,
                                                 bool last = false, bool has_offset_field = true, bool has_length_field = true,
                                                 QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a CRYPTO frame.
   * You have to make sure that the data size won't exceed the maximum size of QUIC packet.
   */
  static QUICCryptoFrameUPtr create_crypto_frame(Ptr<IOBufferBlock> &block, QUICOffset offset, QUICFrameId id = 0,
                                                 QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a ACK frame.
   * You shouldn't call this directly but through QUICAckFrameCreator because QUICAckFrameCreator manages packet numbers that we
   * need to ack.
   */
  static std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create_ack_frame(QUICPacketNumber largest_acknowledged,
                                                                              uint64_t ack_delay, uint64_t first_ack_block,
                                                                              QUICFrameId id            = 0,
                                                                              QUICFrameGenerator *owner = nullptr);
  /*
   * Creates a CONNECTION_CLOSE frame.
   */
  static std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> create_connection_close_frame(
    uint16_t error_code, QUICFrameType frame_type, uint16_t reason_phrase_length = 0, const char *reason_phrase = nullptr,
    QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  static std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> create_connection_close_frame(
    QUICConnectionError &error, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a APPLICATION_CLOSE frame.
   */
  static std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc> create_application_close_frame(
    QUICAppErrorCode error_code, uint16_t reason_phrase_length = 0, const char *reason_phrase = nullptr, QUICFrameId id = 0,
    QUICFrameGenerator *owner = nullptr);
  static std::unique_ptr<QUICApplicationCloseFrame, QUICFrameDeleterFunc> create_application_close_frame(
    QUICConnectionError &error, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a MAX_DATA frame.
   */
  static std::unique_ptr<QUICMaxDataFrame, QUICFrameDeleterFunc> create_max_data_frame(uint64_t maximum_data, QUICFrameId id = 0,
                                                                                       QUICFrameGenerator *owner = nullptr);

  /*
 /  * Creates a MAX_STREAM_DATA frame.
   */
  static std::unique_ptr<QUICMaxStreamDataFrame, QUICFrameDeleterFunc> create_max_stream_data_frame(
    QUICStreamId stream_id, uint64_t maximum_stream_data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);
  /*
   * Creates a MAX_STREAM_ID frame.
   */
  static std::unique_ptr<QUICMaxStreamIdFrame, QUICFrameDeleterFunc> create_max_stream_id_frame(
    QUICStreamId maximum_stream_id, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a PING frame
   */
  static std::unique_ptr<QUICPingFrame, QUICFrameDeleterFunc> create_ping_frame(QUICFrameId id            = 0,
                                                                                QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a PATH_CHALLENGE frame
   */
  static std::unique_ptr<QUICPathChallengeFrame, QUICFrameDeleterFunc> create_path_challenge_frame(
    const uint8_t *data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a PATH_RESPONSE frame
   */
  static std::unique_ptr<QUICPathResponseFrame, QUICFrameDeleterFunc> create_path_response_frame(
    const uint8_t *data, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a BLOCKED frame.
   */
  static std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc> create_blocked_frame(QUICOffset offset, QUICFrameId id = 0,
                                                                                      QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a STREAM_BLOCKED frame.
   */
  static std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc> create_stream_blocked_frame(
    QUICStreamId stream_id, QUICOffset offset, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a STREAM_ID_BLOCKED frame.
   */
  static std::unique_ptr<QUICStreamIdBlockedFrame, QUICFrameDeleterFunc> create_stream_id_blocked_frame(
    QUICStreamId stream_id, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a RST_STREAM frame.
   */
  static std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> create_rst_stream_frame(QUICStreamId stream_id,
                                                                                           QUICAppErrorCode error_code,
                                                                                           QUICOffset final_offset,
                                                                                           QUICFrameId id            = 0,
                                                                                           QUICFrameGenerator *owner = nullptr);
  static std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> create_rst_stream_frame(QUICStreamError &error,
                                                                                           QUICFrameId id            = 0,
                                                                                           QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a STOP_SENDING frame.
   */
  static std::unique_ptr<QUICStopSendingFrame, QUICFrameDeleterFunc> create_stop_sending_frame(QUICStreamId stream_id,
                                                                                               QUICAppErrorCode error_code,
                                                                                               QUICFrameId id            = 0,
                                                                                               QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a NEW_CONNECTION_ID frame.
   */
  static std::unique_ptr<QUICNewConnectionIdFrame, QUICFrameDeleterFunc> create_new_connection_id_frame(
    uint32_t sequence, QUICConnectionId connectoin_id, QUICStatelessResetToken stateless_reset_token, QUICFrameId id = 0,
    QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a NEW_TOKEN frame
   */
  static std::unique_ptr<QUICNewTokenFrame, QUICFrameDeleterFunc> create_new_token_frame(const QUICResumptionToken &token,
                                                                                         QUICFrameId id            = 0,
                                                                                         QUICFrameGenerator *owner = nullptr);

  /*
   * Creates a RETIRE_CONNECTION_ID frame
   */
  static std::unique_ptr<QUICRetireConnectionIdFrame, QUICFrameDeleterFunc> create_retire_connection_id_frame(
    uint64_t seq_num, QUICFrameId id = 0, QUICFrameGenerator *owner = nullptr);

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

class QUICFrameInfo
{
public:
  QUICFrameInfo(QUICFrameId id, QUICFrameGenerator *generator) : _id(id), _generator(generator) {}
  QUICFrameId id();
  QUICFrameGenerator *generated_by();

private:
  QUICFrameId _id = 0;
  QUICFrameGenerator *_generator;
};
