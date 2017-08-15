#include "QUICStreamManager.h"
#include "QUICFlowController.h"
#include "QUICCongestionController.h"
#include "QUICLossDetector.h"
#include "QUICEvents.h"
#include "QUICPacketTransmitter.h"

class MockQUICConnection : public QUICConnection
{
public:
  MockQUICConnection() : QUICConnection() { this->_mutex = new_ProxyMutex(); };

  void
  transmit_packet(std::unique_ptr<const QUICPacket> packet) override
  {
    ++_transmit_count;
  }

  void
  retransmit_packet(const QUICPacket &packet) override
  {
    ++_retransmit_count;
  }

  Ptr<ProxyMutex>
  get_transmitter_mutex() override
  {
    return this->_mutex;
  }

  void
  transmit_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame) override
  {
  }

  void
  handle_frame(std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;
  }

  QUICApplication *
  get_application(QUICStreamId stream_id) override
  {
    return nullptr;
  }

  QUICCrypto *
  get_crypto() override
  {
    return nullptr;
  }

  uint32_t
  minimum_quic_packet_size() override
  {
    return 1200;
  }

  uint32_t
  maximum_quic_packet_size() override
  {
    return 1200;
  }

  uint32_t
  pmtu() override
  {
    return 1280;
  }

  void
  close(QUICError error) override
  {
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

  int _transmit_count   = 0;
  int _retransmit_count = 0;
  Ptr<ProxyMutex> _mutex;
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};

class MockQUICPacketTransmitter : public QUICPacketTransmitter
{
public:
  MockQUICPacketTransmitter() : QUICPacketTransmitter() { this->_mutex = new_ProxyMutex(); };

  void
  transmit_packet(std::unique_ptr<const QUICPacket> packet) override
  {
    ++_transmit_count;
  }

  void
  retransmit_packet(const QUICPacket &packet) override
  {
    ++_retransmit_count;
  }

  Ptr<ProxyMutex>
  get_transmitter_mutex() override
  {
    return this->_mutex;
  }

  int _transmit_count   = 0;
  int _retransmit_count = 0;
  Ptr<ProxyMutex> _mutex;
};

class MockQUICFrameTransmitter : public QUICFrameTransmitter
{
  void
  transmit_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame)
  {
  }
};

class MockQUICLossDetector : public QUICLossDetector
{
public:
  MockQUICLossDetector() : QUICLossDetector(new MockQUICPacketTransmitter()) {}

  void
  rcv_frame(std::shared_ptr<const QUICFrame>)
  {
  }

  void
  on_packet_sent(std::unique_ptr<const QUICPacket> packet)
  {
  }
};

class MockQUICStreamManager : public QUICStreamManager
{
public:
  MockQUICStreamManager() : QUICStreamManager() {}

  // Override
  virtual void
  handle_frame(std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;
  }

  // for Test
  int
  getStreamFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::STREAM)];
  }

  int
  getAckFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::ACK)];
  }

  int
  getPingFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::PING)];
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

private:
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};

class MockQUICCongestionController : public QUICCongestionController
{
public:
  MockQUICCongestionController() : QUICCongestionController() {}

  // Override
  virtual void
  handle_frame(std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;
  }

  // for Test
  int
  getStreamFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::STREAM)];
  }

  int
  getAckFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::ACK)];
  }

  int
  getPingFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::PING)];
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

private:
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};

class MockQUICFlowController : public QUICFlowController
{
public:
  MockQUICFlowController() {}

  // Override
  virtual void
  handle_frame(std::shared_ptr<const QUICFrame> f) override
  {
    ++_frameCount[static_cast<int>(f->type())];
    ++_totalFrameCount;
  }

  // for Test
  int
  getStreamFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::STREAM)];
  }

  int
  getAckFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::ACK)];
  }

  int
  getPingFrameCount()
  {
    return _frameCount[static_cast<int>(QUICFrameType::PING)];
  }

  int
  getTotalFrameCount()
  {
    return _totalFrameCount;
  }

private:
  int _totalFrameCount = 0;
  int _frameCount[256] = {0};
};
