#pragma once

#include "QLog.h"
#include "QLogUtils.h"
#include "QUICPacket.h"
#include "QUICContext.h"
#include "QUICFrameReader.h"

namespace QLog
{
class QLogListener : public QUICCallback
{
public:
  QLogListener(QUICContext &ctx, std::string odcid, std::string title = "", std::string desc = "") : _context(ctx)
  {
    this->_log.new_trace(odcid, title, desc); // initial new trace
  }

  void
  packet_send_callback(QUICCallbackContext &, const QUICPacket &packet) override
  {
    QUICFrameReaderUnbond reader(packet);
    // This packet is encrypted packet. We don't have enough info to decrypted it
    // TODO: In this way we need separate the packet encryption and packet generating into two step.
    // And insert the callback into these two steps.
    // see QUICPacketFactory. For Now, emit the frame info .
    auto qe = std::make_unique<Transport::PacketSent>(PacketTypeToName(packet.type()), QUICPacketToLogPacket(packet));
    this->_log.last_trace().push_event(std::move(qe));
  };

  void
  packet_recv_callback(QUICCallbackContext &, const QUICPacket &packet) override
  {
    QUICFrameReaderUnbond reader(packet);
    uint8_t buf[2048];
    auto qe = std::make_unique<Transport::PacketReceived>(PacketTypeToName(packet.type()), QUICPacketToLogPacket(packet));
    for (auto frame = reader.read_frame(buf); frame; frame = reader.read_frame(buf)) {
      qe->append_frames(QLogFrameFactory::create(frame));
    }
    this->_log.last_trace().push_event(std::move(qe));
  };

  void
  connection_close_callback(QUICCallbackContext &) override
  {
    this->_log.dump(this->_context.config()->qlog_file());
  }

  Trace &
  last_trace()
  {
    return this->_log.last_trace();
  }

private:
  QLog _log;
  QUICContext &_context;
};

} // namespace QLog
