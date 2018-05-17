#include "QUICPacketReceiveQueue.h"

QUICPacketReceiveQueue::QUICPacketReceiveQueue(QUICPacketFactory &packet_factory) : _packet_factory(packet_factory) {}

void
QUICPacketReceiveQueue::enqueue(UDPPacket *packet)
{
  this->_queue.enqueue(packet);
}

QUICPacketUPtr
QUICPacketReceiveQueue::dequeue(QUICPacketCreationResult &result)
{
  QUICPacketUPtr quic_packet = QUICPacketFactory::create_null_packet();
  UDPPacket *udp_packet      = this->_queue.dequeue();
  if (!udp_packet) {
    result = QUICPacketCreationResult::NOT_READY;
    return quic_packet;
  }

  // Create a QUIC packet
  ats_unique_buf pkt = ats_unique_malloc(udp_packet->getPktLength());
  IOBufferBlock *b   = udp_packet->getIOBlockChain();
  size_t written     = 0;
  while (b) {
    memcpy(pkt.get() + written, b->buf(), b->read_avail());
    written += b->read_avail();
    b = b->next.get();
  }

  quic_packet =
    this->_packet_factory.create(udp_packet->from, std::move(pkt), written, this->largest_received_packet_number(), result);
  udp_packet->free();

  if (result == QUICPacketCreationResult::NOT_READY) {
    // FIXME: unordered packet should be buffered and retried
    if (this->_queue.size > 0) {
      result = QUICPacketCreationResult::IGNORED;
    }
  }

  if (quic_packet->packet_number() > this->_largest_received_packet_number) {
    this->_largest_received_packet_number = quic_packet->packet_number();
  }

  return quic_packet;
}

uint32_t
QUICPacketReceiveQueue::size()
{
  return this->_queue.size;
}

QUICPacketNumber
QUICPacketReceiveQueue::largest_received_packet_number()
{
  return this->_largest_received_packet_number;
}

void
QUICPacketReceiveQueue::reset()
{
  this->_largest_received_packet_number = 0;
}
