#pragma once

#include <memory>
#include <yaml-cpp/yaml.h>

#include "QUICFrame.h"

namespace QLog
{
class QLogFrame
{
public:
  QLogFrame(QUICFrameType type) : _type(type) {}
  virtual ~QLogFrame() {}

  QUICFrameType
  type() const
  {
    return this->_type;
  }

  // encode frame into YAML stype
  virtual void encode(YAML::Node &node) = 0;

protected:
  QUICFrameType _type = QUICFrameType::UNKNOWN;
};

using QLogFrameUPtr = std::unique_ptr<QLogFrame>;

//
// convert QUICFrame to QLogFrame
//
class QLogFrameFactory
{
public:
  // create QLogFrame
  static QLogFrameUPtr create(QUICFrame &frame);
};

namespace Frame
{
  struct AckFrame : public QLogFrame {
    AckFrame(QUICAckFrame &frame) : QLogFrame(frame.type())
    {
      acked_range = frame.ranges();
      ack_delay   = frame.ack_delay();
      if (frame.ecn_section()) {
        ect0 = frame.ecn_section()->ect0_count();
        ect1 = frame.ecn_section()->ect1_count();
        ce   = frame.ecn_section()->ecn_ce_count();
      }
    }

    void encode(YAML::Node &) override;

    std::set<QUICAckFrame::PacketNumberRange> acked_range;
    uint64_t ect1      = 0;
    uint64_t ect0      = 0;
    uint64_t ce        = 0;
    uint64_t ack_delay = 0;
  };

  struct StreamFrame : public QLogFrame {
    StreamFrame(QUICStreamFrame &frame) : QLogFrame(frame.type())
    {
      stream_id = std::to_string(static_cast<uint64_t>(frame.stream_id()));
      offset    = std::to_string(static_cast<uint64_t>(frame.offset()));
      length    = frame.data_length();
      fin       = frame.has_fin_flag();
    }

    void encode(YAML::Node &) override;
    std::string stream_id;

    // These two MUST always be set
    // If not present in the Frame type, log their default values
    std::string offset;
    uint64_t length = 0;

    // this MAY be set any time, but MUST only be set if the value is "true"
    // if absent, the value MUST be assumed to be "false"
    bool fin = false;

    // FIXME raw
  };

  struct PaddingFrame : public QLogFrame {
    PaddingFrame(QUICPaddingFrame &frame) : QLogFrame(frame.type()) {}
    void encode(YAML::Node &) override;
  };

  struct PingFrame : public QLogFrame {
    PingFrame(QUICPingFrame &frame) : QLogFrame(frame.type()) {}
    void encode(YAML::Node &) override;
  };

  struct RstStreamFrame : public QLogFrame {
    RstStreamFrame(QUICRstStreamFrame &frame) : QLogFrame(frame.type())
    {
      stream_id  = std::to_string(static_cast<uint64_t>(frame.stream_id()));
      error_code = frame.error_code();
      final_size = std::to_string(frame.final_offset());
    }

    void encode(YAML::Node &) override;
    std::string stream_id;
    // FIXME ApplicationError
    uint64_t error_code = 0;
    std::string final_size;
  };

  struct StopSendingFrame : public QLogFrame {
    StopSendingFrame(QUICStopSendingFrame &frame) : QLogFrame(frame.type())
    {
      stream_id  = std::to_string(static_cast<uint64_t>(frame.stream_id()));
      error_code = frame.error_code();
    }

    void encode(YAML::Node &) override;
    std::string stream_id;
    // FIXME ApplicationError
    uint64_t error_code = 0;
  };

  struct CryptoFrame : public QLogFrame {
    CryptoFrame(QUICCryptoFrame &frame) : QLogFrame(frame.type())
    {
      offset = std::to_string(static_cast<uint64_t>(frame.offset()));
      length = frame.data_length();
    }

    void encode(YAML::Node &) override;
    std::string offset;
    uint64_t length = 0;
  };

  struct NewTokenFrame : public QLogFrame {
    NewTokenFrame(QUICNewTokenFrame &frame) : QLogFrame(frame.type())
    {
      token  = QUICBase::to_hex(frame.token(), frame.token_length());
      length = frame.token_length();
    }

    void encode(YAML::Node &) override;
    std::string token;
    uint64_t length = 0;
  };

  struct MaxDataFrame : public QLogFrame {
    MaxDataFrame(QUICMaxDataFrame &frame) : QLogFrame(frame.type()) { maximum = std::to_string(frame.maximum_data()); }

    void encode(YAML::Node &) override;
    std::string maximum;
  };

  struct MaxStreamDataFrame : public QLogFrame {
    MaxStreamDataFrame(QUICMaxStreamDataFrame &frame) : QLogFrame(frame.type())
    {
      stream_id = std::to_string(static_cast<uint64_t>(frame.stream_id()));
      maximum   = std::to_string(frame.maximum_stream_data());
    }

    void encode(YAML::Node &) override;
    std::string stream_id;
    std::string maximum;
  };

  struct MaxStreamsFrame : public QLogFrame {
    MaxStreamsFrame(QUICMaxStreamsFrame &frame) : QLogFrame(frame.type())
    {
      maximum = std::to_string(frame.maximum_streams());
      // FIXME
      stream_type = "bidirectional";
    }

    void encode(YAML::Node &) override;
    std::string stream_type;
    std::string maximum;
  };

  struct DataBlockedFrame : public QLogFrame {
    DataBlockedFrame(QUICDataBlockedFrame &frame) : QLogFrame(frame.type())
    {
      limit = std::to_string(static_cast<uint64_t>(frame.offset()));
    }
    void encode(YAML::Node &) override;
    std::string limit;
  };

  struct StreamDataBlockedFrame : public QLogFrame {
    StreamDataBlockedFrame(QUICStreamDataBlockedFrame &frame) : QLogFrame(frame.type())
    {
      limit     = std::to_string(static_cast<uint64_t>(frame.offset()));
      stream_id = std::to_string(static_cast<uint64_t>(frame.stream_id()));
    }

    void encode(YAML::Node &) override;
    std::string stream_id, limit;
  };

  struct StreamsBlockedFrame : public QLogFrame {
    StreamsBlockedFrame(QUICStreamIdBlockedFrame &frame) : QLogFrame(frame.type())
    {
      stream_type = "bidirectional";
      stream_id   = std::to_string(static_cast<uint64_t>(frame.stream_id()));
    }

    void encode(YAML::Node &) override;
    std::string stream_id, stream_type;
  };

  struct NewConnectionIDFrame : public QLogFrame {
    NewConnectionIDFrame(QUICNewConnectionIdFrame &frame) : QLogFrame(frame.type())
    {
      sequence_number       = std::to_string(frame.sequence());
      retire_prior_to       = std::to_string(frame.retire_prior_to());
      connection_id         = frame.connection_id().hex();
      stateless_reset_token = QUICBase::to_hex(frame.stateless_reset_token().buf(), QUICStatelessResetToken::LEN);
      length                = frame.connection_id().length();
    }

    void encode(YAML::Node &) override;
    std::string sequence_number, retire_prior_to, connection_id, stateless_reset_token;
    uint8_t length = 0;
  };

  struct RetireConnectionIDFrame : public QLogFrame {
    RetireConnectionIDFrame(QUICRetireConnectionIdFrame &frame) : QLogFrame(frame.type())
    {
      sequence_number = std::to_string(frame.seq_num());
    }
    void encode(YAML::Node &) override;
    std::string sequence_number;
  };

  struct PathChallengeFrame : public QLogFrame {
    PathChallengeFrame(QUICPathChallengeFrame &frame) : QLogFrame(frame.type())
    {
      data = QUICBase::to_hex(frame.data(), QUICPathChallengeFrame::DATA_LEN);
    }
    void encode(YAML::Node &) override;
    std::string data;
  };

  struct PathResponseFrame : public QLogFrame {
    PathResponseFrame(QUICPathResponseFrame &frame) : QLogFrame(frame.type())
    {
      data = QUICBase::to_hex(frame.data(), QUICPathChallengeFrame::DATA_LEN);
    }
    void encode(YAML::Node &) override;
    std::string data;
  };

  struct ConnectionCloseFrame : public QLogFrame {
    ConnectionCloseFrame(QUICConnectionCloseFrame &frame, bool app = false) : QLogFrame(frame.type())
    {
      error_space = app ? "application" : "transport";
      error_code  = frame.error_code();
      // FIXME
      raw_error_code = error_code;
      reason         = frame.reason_phrase();
    }

    void encode(YAML::Node &) override;
    std::string error_space, reason, trigger_frame_type;
    uint64_t error_code, raw_error_code;
  };

  struct HandshakeDoneFrame : public QLogFrame {
    HandshakeDoneFrame(QUICHandshakeDoneFrame &frame) : QLogFrame(frame.type()){};
    void encode(YAML::Node &) override;
  };

  struct UnknownFrame : public QLogFrame {
    UnknownFrame(QUICUnknownFrame &frame) : QLogFrame(frame.type())
    {
      // FIXME
      raw_frame_type = static_cast<uint8_t>(frame.type());
    }

    void encode(YAML::Node &) override;
    uint8_t raw_frame_type = 0;
  };
} // namespace Frame
} // namespace QLog
