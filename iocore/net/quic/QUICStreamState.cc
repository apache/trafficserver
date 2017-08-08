#include "QUICStreamState.h"
#include "ts/ink_assert.h"

const QUICStreamState::State
QUICStreamState::get() const
{
  return this->_state;
}

bool
QUICStreamState::is_allowed_to_send(const QUICFrame &frame) const
{
  return true;
}

bool
QUICStreamState::is_allowed_to_receive(const QUICFrame &frame) const
{
  return true;
}

void
QUICStreamState::update_with_received_frame(const QUICFrame &frame)
{
  switch (this->_state) {
  case State::idle:
    this->_set_state(State::open);
  // fall through
  case State::open: {
    if (frame.type() == QUICFrameType::STREAM && dynamic_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
      this->_set_state(State::half_closed_remote);
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::closed);
    }
  } break;
  case State::half_closed_local:
  case State::half_closed_remote:
  case State::closed:
  case State::illegal:
    // Once we get illegal state, no way to recover it
    break;
  default:
    break;
  }
}

void
QUICStreamState::update_with_sent_frame(const QUICFrame &frame)
{
}

void
QUICStreamState::_set_state(State s)
{
  ink_assert(s != State::idle);
  ink_assert(s != State::illegal);
  this->_state = s;
}
