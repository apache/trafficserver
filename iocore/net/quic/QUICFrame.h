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
  QUICFrame(const uint8_t *buf, size_t len) : _buf(buf), _len(len){};
  virtual QUICFrameType type() const;
  virtual size_t size() const = 0;
  virtual void store(uint8_t *buf, size_t *len) const = 0;
  virtual void reset(const uint8_t *buf, size_t len);
  static QUICFrameType type(const uint8_t *buf);
  virtual ~QUICFrame() {}

  LINK(QUICFrame, link);

protected:
  QUICFrame() {}
  const uint8_t *_buf = nullptr;
  size_t _len         = 0;
};

//
// STREAM Frame
//

class QUICStreamFrame : public QUICFrame
{
public:
  QUICStreamFrame() : QUICFrame() {}
  QUICStreamFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICStreamFrame(const uint8_t *buf, size_t len, QUICStreamId streamid, QUICOffset offset, bool last = false);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  void store(uint8_t *buf, size_t *len, bool include_length_field) const;
  QUICStreamId stream_id() const;
  QUICOffset offset() const;
  const uint8_t *data() const;
  size_t data_length() const;
  bool has_data_length_field() const;
  bool has_fin_flag() const;

  LINK(QUICStreamFrame, link);

private:
  const uint8_t *_data    = nullptr;
  size_t _data_len        = 0;
  QUICStreamId _stream_id = 0;
  QUICOffset _offset      = 0;
  bool _fin               = false;
  size_t _get_data_offset() const;
  size_t _get_stream_id_offset() const;
  size_t _get_offset_offset() const;
  size_t _get_stream_id_len() const;
  size_t _get_offset_len() const;
};

//
// ACK Frame
//

class QUICAckFrame : public QUICFrame
{
public:
  class AckBlock
  {
  public:
    AckBlock(const uint8_t *buf, uint8_t ack_block_length);
    AckBlock(uint8_t gap, uint64_t length);
    uint8_t gap() const;
    uint64_t length() const;
    LINK(QUICAckFrame::AckBlock, link);

  private:
    uint64_t _data = 0;
  };

  class AckBlockSection
  {
  public:
    class const_iterator : public std::iterator<std::input_iterator_tag, QUICAckFrame::AckBlock>
    {
    public:
      const_iterator(uint8_t index, const uint8_t *buf, uint8_t num_blocks, uint8_t ack_block_length);
      const_iterator(uint8_t index, const std::vector<QUICAckFrame::AckBlock> *ack_blocks);

      const QUICAckFrame::AckBlock &operator*() const { return this->_current_block; };
      const QUICAckFrame::AckBlock *operator->() const { return &this->_current_block; };
      const QUICAckFrame::AckBlock &operator++()
      {
        ++(this->_index);

        if (this->_buf) {
          // TODO Parse Ack Block
        } else {
          if (this->_ack_blocks->size() == this->_index) {
            this->_current_block = {static_cast<uint8_t>(0), 0ULL};
          } else {
            this->_current_block = this->_ack_blocks->at(this->_index);
          }
        }

        return this->_current_block;
      };

      const bool
      operator!=(const const_iterator &ite) const
      {
        return this->_index != ite._index;
      };

      const bool
      operator==(const const_iterator &ite) const
      {
        return this->_index == ite._index;
      };

    private:
      uint8_t _index;
      const uint8_t *_buf;
      const std::vector<QUICAckFrame::AckBlock> *_ack_blocks = nullptr;
      QUICAckFrame::AckBlock _current_block                  = {static_cast<uint8_t>(0), 0ULL};
    };

    AckBlockSection(uint64_t first_ack_block_length);
    AckBlockSection(const uint8_t *buf, uint8_t num_blocks, uint8_t ack_block_length);
    uint8_t count() const;
    size_t size() const;
    void store(uint8_t *buf, size_t *len) const;
    uint64_t first_ack_block_length() const;
    void add_ack_block(const AckBlock block);
    const_iterator begin() const;
    const_iterator end() const;

  private:
    const uint8_t *_buf              = nullptr;
    uint64_t _first_ack_block_length = 0;
    uint8_t _num_blocks              = 0;
    uint8_t _ack_block_length        = 0;
    std::vector<QUICAckFrame::AckBlock> _ack_blocks;
  };

  class TimestampSection
  {
  public:
    uint8_t count() const;
    size_t size() const;
    void store(uint8_t *buf, size_t *len) const;
    void add_timestamp();
  };

  QUICAckFrame() : QUICFrame() {}
  QUICAckFrame(const uint8_t *buf, size_t len);
  QUICAckFrame(QUICPacketNumber largest_acknowledged, uint16_t ack_delay, uint64_t first_ack_block_length);
  virtual ~QUICAckFrame();
  virtual void reset(const uint8_t *buf, size_t len) override;
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  uint8_t num_blocks() const;
  uint8_t num_timestamps() const;
  QUICPacketNumber largest_acknowledged() const;
  uint16_t ack_delay() const;
  const AckBlockSection *ack_block_section() const;
  AckBlockSection *ack_block_section();
  const TimestampSection *timestamp_section() const;
  bool has_ack_blocks() const;

private:
  size_t _get_num_timestamp_offset() const;
  size_t _get_largest_acknowledged_offset() const;
  size_t _get_largest_acknowledged_length() const;
  size_t _get_ack_block_length() const;
  size_t _get_ack_delay_offset() const;
  size_t _get_ack_block_section_offset() const;
  size_t _get_timestamp_section_offset() const;
  QUICPacketNumber _largest_acknowledged = 0;
  uint16_t _ack_delay                    = 0;
  AckBlockSection *_ack_block_section    = nullptr;
  TimestampSection *_timestamp_section   = nullptr;
};

//
// RST_STREAM
//

class QUICRstStreamFrame : public QUICFrame
{
public:
  QUICRstStreamFrame() : QUICFrame() {}
  QUICRstStreamFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICRstStreamFrame(QUICStreamId stream_id, QUICErrorCode error_code, QUICOffset final_offset);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  QUICErrorCode error_code() const;
  QUICStreamId stream_id() const;
  QUICOffset final_offset() const;

private:
  QUICStreamId _stream_id = 0;
  QUICErrorCode _error_code;
  QUICOffset _final_offset = 0;
};

//
// PING
//

class QUICPingFrame : public QUICFrame
{
public:
  QUICPingFrame() : QUICFrame() {}
  QUICPingFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
};

// PADDING

class QUICPaddingFrame : public QUICFrame
{
public:
  QUICPaddingFrame() : QUICFrame() {}
  QUICPaddingFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
};

//
// CONNECTION_CLOSE
//

class QUICConnectionCloseFrame : public QUICFrame
{
public:
  QUICConnectionCloseFrame() : QUICFrame() {}
  QUICConnectionCloseFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICConnectionCloseFrame(QUICErrorCode error_code, uint16_t reason_phrase_length, const char *reason_phrase);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  QUICErrorCode error_code() const;
  uint16_t reason_phrase_length() const;
  const char *reason_phrase() const;

private:
  QUICErrorCode _error_code;
  uint16_t _reason_phrase_length = 0;
  const char *_reason_phrase     = nullptr;
};

//
// MAX_DATA
//

class QUICMaxDataFrame : public QUICFrame
{
public:
  QUICMaxDataFrame() : QUICFrame() {}
  QUICMaxDataFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICMaxDataFrame(uint64_t maximum_data);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  uint64_t maximum_data() const;

private:
  uint64_t _maximum_data = 0;
};

//
// MAX_STREAM_DATA
//

class QUICMaxStreamDataFrame : public QUICFrame
{
public:
  QUICMaxStreamDataFrame() : QUICFrame() {}
  QUICMaxStreamDataFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICMaxStreamDataFrame(QUICStreamId stream_id, uint64_t maximum_stream_data);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  QUICStreamId stream_id() const;
  uint64_t maximum_stream_data() const;

private:
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
  QUICMaxStreamIdFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICMaxStreamIdFrame(QUICStreamId maximum_stream_id);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  QUICStreamId maximum_stream_id() const;

private:
  QUICStreamId _maximum_stream_id = 0;
};

//
// BLOCKED
//
class QUICBlockedFrame : public QUICFrame
{
public:
  QUICBlockedFrame() : QUICFrame() {}
  QUICBlockedFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
};

//
// STREAM_BLOCKED
//

class QUICStreamBlockedFrame : public QUICFrame
{
public:
  QUICStreamBlockedFrame() : QUICFrame() {}
  QUICStreamBlockedFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICStreamBlockedFrame(QUICStreamId stream_id);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  QUICStreamId stream_id() const;

private:
  QUICStreamId _stream_id;
};

//
// STREAM_ID_NEEDED
//
class QUICStreamIdNeededFrame : public QUICFrame
{
public:
  QUICStreamIdNeededFrame() : QUICFrame() {}
  QUICStreamIdNeededFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
};

//
// NEW_CONNECTION_ID
//

class QUICNewConnectionIdFrame : public QUICFrame
{
public:
  QUICNewConnectionIdFrame() : QUICFrame() {}
  QUICNewConnectionIdFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICNewConnectionIdFrame(uint16_t sequence, QUICConnectionId connection_id);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  uint16_t sequence() const;
  QUICConnectionId connection_id() const;

private:
  uint16_t _sequence              = 0;
  QUICConnectionId _connection_id = 0;
};

//
// STOP_SENDING
//

class QUICStopSendingFrame : public QUICFrame
{
public:
  QUICStopSendingFrame() : QUICFrame() {}
  QUICStopSendingFrame(const uint8_t *buf, size_t len) : QUICFrame(buf, len) {}
  QUICStopSendingFrame(QUICStreamId stream_id, QUICErrorCode error_code);
  virtual QUICFrameType type() const override;
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  QUICStreamId stream_id() const;
  QUICErrorCode error_code() const;

private:
  QUICStreamId _stream_id = 0;
  QUICErrorCode _error_code;
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
  virtual size_t size() const override;
  virtual void store(uint8_t *buf, size_t *len) const override;
  QUICPacketType packet_type() const;

private:
  QUICFrameUPtr _frame = QUICFrameUPtr(nullptr, nullptr);
  ats_unique_buf _data = ats_unique_buf(nullptr, [](void *p) { ats_free(p); });
  size_t _size;
  QUICPacketType _packet_type;
};

extern ClassAllocator<QUICStreamFrame> quicStreamFrameAllocator;
extern ClassAllocator<QUICAckFrame> quicAckFrameAllocator;
extern ClassAllocator<QUICPaddingFrame> quicPaddingFrameAllocator;
extern ClassAllocator<QUICRstStreamFrame> quicRstStreamFrameAllocator;
extern ClassAllocator<QUICConnectionCloseFrame> quicConnectionCloseFrameAllocator;
extern ClassAllocator<QUICMaxDataFrame> quicMaxDataFrameAllocator;
extern ClassAllocator<QUICMaxStreamDataFrame> quicMaxStreamDataFrameAllocator;
extern ClassAllocator<QUICMaxStreamIdFrame> quicMaxStreamIdFrameAllocator;
extern ClassAllocator<QUICPingFrame> quicPingFrameAllocator;
extern ClassAllocator<QUICBlockedFrame> quicBlockedFrameAllocator;
extern ClassAllocator<QUICStreamBlockedFrame> quicStreamBlockedFrameAllocator;
extern ClassAllocator<QUICStreamIdNeededFrame> quicStreamIdNeededFrameAllocator;
extern ClassAllocator<QUICNewConnectionIdFrame> quicNewConnectionIdFrameAllocator;
extern ClassAllocator<QUICStopSendingFrame> quicStopSendingFrameAllocator;
extern ClassAllocator<QUICRetransmissionFrame> quicRetransmissionFrameAllocator;

class QUICFrameDeleter
{
public:
  // TODO Probably these methods should call destructor
  static void
  delete_null_frame(QUICFrame *frame)
  {
  }

  static void
  delete_stream_frame(QUICFrame *frame)
  {
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
    quicPaddingFrameAllocator.free(static_cast<QUICPaddingFrame *>(frame));
  }

  static void
  delete_rst_stream_frame(QUICFrame *frame)
  {
    quicRstStreamFrameAllocator.free(static_cast<QUICRstStreamFrame *>(frame));
  }

  static void
  delete_connection_close_frame(QUICFrame *frame)
  {
    quicConnectionCloseFrameAllocator.free(static_cast<QUICConnectionCloseFrame *>(frame));
  }

  static void
  delete_max_data_frame(QUICFrame *frame)
  {
    quicMaxDataFrameAllocator.free(static_cast<QUICMaxDataFrame *>(frame));
  }

  static void
  delete_max_stream_data_frame(QUICFrame *frame)
  {
    quicMaxStreamDataFrameAllocator.free(static_cast<QUICMaxStreamDataFrame *>(frame));
  }

  static void
  delete_max_stream_id_frame(QUICFrame *frame)
  {
    quicMaxStreamIdFrameAllocator.free(static_cast<QUICMaxStreamIdFrame *>(frame));
  }

  static void
  delete_ping_frame(QUICFrame *frame)
  {
    quicPingFrameAllocator.free(static_cast<QUICPingFrame *>(frame));
  }

  static void
  delete_blocked_frame(QUICFrame *frame)
  {
    quicBlockedFrameAllocator.free(static_cast<QUICBlockedFrame *>(frame));
  }

  static void
  delete_stream_blocked_frame(QUICFrame *frame)
  {
    quicStreamBlockedFrameAllocator.free(static_cast<QUICStreamBlockedFrame *>(frame));
  }

  static void
  delete_stream_id_needed_frame(QUICFrame *frame)
  {
    quicStreamIdNeededFrameAllocator.free(static_cast<QUICStreamIdNeededFrame *>(frame));
  }

  static void
  delete_new_connection_id_frame(QUICFrame *frame)
  {
    quicNewConnectionIdFrameAllocator.free(static_cast<QUICNewConnectionIdFrame *>(frame));
  }

  static void
  delete_stop_sending_frame(QUICFrame *frame)
  {
    quicStopSendingFrameAllocator.free(static_cast<QUICStopSendingFrame *>(frame));
  }

  static void
  delete_retransmission_frame(QUICFrame *frame)
  {
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
                                                 bool last = false);
  /*
   * Creates a ACK frame.
   * You shouldn't call this directly but through QUICAckFrameCreator because QUICAckFrameCreator manages packet numbers that we
   * need to ack.
   */
  static std::unique_ptr<QUICAckFrame, QUICFrameDeleterFunc> create_ack_frame(QUICPacketNumber largest_acknowledged,
                                                                              uint16_t ack_delay, uint64_t first_ack_block_length);
  /*
   * Creates a CONNECTION_CLOSE frame.
   */
  static std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> create_connection_close_frame(
    QUICErrorCode error_code, uint16_t reason_phrase_length = 0, const char *reason_phrase = nullptr);
  static std::unique_ptr<QUICConnectionCloseFrame, QUICFrameDeleterFunc> create_connection_close_frame(
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
   * Creates a BLOCKED frame.
   */
  static std::unique_ptr<QUICBlockedFrame, QUICFrameDeleterFunc> create_blocked_frame();

  /*
   * Creates a STREAM_BLOCKED frame.
   */
  static std::unique_ptr<QUICStreamBlockedFrame, QUICFrameDeleterFunc> create_stream_blocked_frame(QUICStreamId stream_id);

  /*
   * Creates a RST_STREAM frame.
   */
  static std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> create_rst_stream_frame(QUICStreamId stream_id,
                                                                                           QUICErrorCode error_code,
                                                                                           QUICOffset final_offset);
  static std::unique_ptr<QUICRstStreamFrame, QUICFrameDeleterFunc> create_rst_stream_frame(QUICStreamErrorUPtr error);

  /*
   * Creates a STOP_SENDING frame.
   */
  static std::unique_ptr<QUICStopSendingFrame, QUICFrameDeleterFunc> create_stop_sending_frame(QUICStreamId stream_id,
                                                                                               QUICErrorCode error_code);

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
