#include "QUICLog.h"
#include "QUICPacket.h"

namespace QLog
{
inline static const char *
PacketTypeToName(QUICPacketType pt)
{
  switch (pt) {
  case QUICPacketType::INITIAL:
    return "initial";
  case QUICPacketType::HANDSHAKE:
    return "handshake";
  case QUICPacketType::ZERO_RTT_PROTECTED:
    return "0rtt";
  case QUICPacketType::PROTECTED:
    return "1rtt";
  case QUICPacketType::RETRY:
    return "retry";
  case QUICPacketType::VERSION_NEGOTIATION:
    return "version_negotiation";
  case QUICPacketType::STATELESS_RESET:
    return "stateless_reset";
  default:
    return "unknown";
  }
}

inline static QLog::PacketHeader
QUICPacketToLogPacket(const QUICPacket &packet)
{
  QLog::PacketHeader ph;
  ph.dcid           = packet.destination_cid().hex();
  ph.packet_number  = std::to_string(packet.packet_number());
  ph.packet_size    = packet.size();
  ph.payload_length = packet.payload_length();
  return ph;
}

} // namespace QLog