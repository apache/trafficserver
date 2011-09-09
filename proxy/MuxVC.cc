/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/****************************************************************************

    MuxVC.cc

    Description:


  ****************************************************************************/

#include "MuxVC.h"
#include "HttpAccept.h"
#include "Main.h"
#include "NetVCTest.h"
#include "StatPages.h"

/* For stat pages */
#include "UnixNet.h"

const int MUX_LOCK_RETRY = HRTIME_MSECONDS(10);
const int MUX_MAX_DATA_SIZE = USHRT_MAX - sizeof(MuxMessage);

#define MIN(x,y) (x <= y) ? x : y;
#define MAX(x,y) (x >= y) ? x : y;

#define MUX_MAX_BYTES_SLOT 32768
#define MUX_MAX_BYTES_BANK 32768
#define MUX_SMALL_BLOCK_SIZE 256
#define MUX_WRITE_HIGH_WATER  (MUX_MAX_BYTES_SLOT * 4)

void mux_pages_init();

static const char *
control_msg_id_to_string(int msg_type)
{

  switch (msg_type) {
  case INKMUX_MSG_OPEN_CHANNEL:
    return "INKMUX_MSG_OPEN_CHANNEL";
  case INKMUX_MSG_CLOSE_CHANNEL:
    return "INKMUX_MSG_CLOSE_CHANNEL";
  case INKMUX_MSG_SHUTDOWN_WRITE:
    return "INKMUX_MSG_SHUTDOWN_WRITE";
  case INKMUX_MSG_NORMAL_DATA:
    return "INKMUX_MSG_NORMAL_DATA";
  case INKMUX_MSG_OOB_DATA:
    return "INKMUX_MSG_OOB_DATA";
  case INKMUX_MSG_CHANNEL_RESET:
    return "INKMUX_MSG_CHANNEL_RESET";
  case INKMUX_MSG_FLOW_CONTROL_START:
    return "INKMUX_MSG_FLOW_CONTROL_START";
  case INKMUX_MSG_FLOW_CONTROL_STOP:
    return "INKMUX_MSG_FLOW_CONTROL_STOP";
  default:
    "INKMUX_MSG_UNKNOWN";
  }
}

// static void mux_move_data(MIOBuffer* copy_to, IOBufferReader* from, int nbytes)
//
//  Utility routine used to move data from a IOBufferReader to a MIOBuffer
//  If the amount of data is large enough we want to move it by reference.
//  If it's small, small blocks are problematic so copy the data
//
static void
mux_move_data(MIOBuffer * copy_to, IOBufferReader * from, int nbytes)
{

  if (0 && nbytes > MUX_SMALL_BLOCK_SIZE) {
    copy_to->write(from, nbytes);
    from->consume(nbytes);
  } else {
    int left = nbytes;

    while (left > 0) {
      char *block_start = from->start();
      int64_t block_avail = from->block_read_avail();
      int act_on = MIN(block_avail, left);
      int r = copy_to->write(block_start, act_on);
      ink_debug_assert(r == act_on);
      from->consume(act_on);
      left -= act_on;
    }
  }
}

MuxClientState::MuxClientState():
vio(), shutdown(false), enabled(0), flow_stopped(0)     // parent MuxVC lock
{
};

MuxClientVC::MuxClientVC():
link(),
id(-1),
magic(MUX_VC_CLIENT_MAGIC_ALIVE),
closed(false),
other_side_closed(0),
reentrancy_count(0),
need_boost(true),
mux_vc(NULL),
read_state(),
write_state(),
read_byte_bank(NULL),
byte_bank_reader(NULL),
active_timeout(0), inactive_timeout(0), active_event(NULL), inactive_event(NULL), retry_event(NULL), NetVConnection()
{
  SET_HANDLER(MuxClientVC::main_handler);
}

MuxClientVC::~MuxClientVC()
{
}

void
MuxClientVC::init(MuxVC * mvc, int32_t id_arg)
{

  ink_debug_assert(!closed);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  mux_vc = mvc;
  mutex = mux_vc->mutex;

  id = id_arg;
}

// void MuxClientVC::kill()
//
//   Cleans up and deallocates.
//
//   Callee MUST be hold this->mutex &
//     must have already remove this MuxClientVC
//     from it's parent's vc list
//
void
MuxClientVC::kill()
{

  ink_debug_assert(closed == true);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);
  ink_debug_assert(mutex->thread_holding == this_ethread());

  Debug("mux_alloc", "[%d,%d] Killing client id", mux_vc->id, id);

  magic = MUX_VC_CLIENT_MAGIC_DEAD;

  if (read_byte_bank) {
    free_MIOBuffer(read_byte_bank);
    read_byte_bank = NULL;
    byte_bank_reader = NULL;
  }

  if (active_event != NULL) {
    active_event->cancel();
    active_event = NULL;
  }

  if (inactive_event != NULL) {
    inactive_event->cancel();
    inactive_event = NULL;
  }

  if (retry_event != NULL) {
    retry_event->cancel();
    retry_event = NULL;
  }

  mux_vc = NULL;

  read_state.vio, mutex = NULL;
  write_state.vio.mutex = NULL;
  mutex = NULL;

  delete this;
}


VIO *
MuxClientVC::do_io_read(Continuation * c, int64_t nbytes, MIOBuffer * buf)
{

  ink_debug_assert(!closed);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  if (read_state.vio.op == VIO::READ) {
    Debug("mux_last", "do_io_read over nbytes %d ndone %d byte_bank %d",
          read_state.vio.nbytes, read_state.vio.ndone, (byte_bank_reader == NULL) ? 0 : byte_bank_reader->read_avail());
  }

  if (buf) {
    read_state.vio.buffer.writer_for(buf);
    read_state.enabled = 1;
  } else {
    read_state.vio.buffer.clear();
    read_state.enabled = 0;
  }

  read_state.vio.op = VIO::READ;
  read_state.vio.mutex = c->mutex;
  read_state.vio._cont = c;
  read_state.vio.nbytes = nbytes;
  read_state.vio.data = 0;
  read_state.vio.ndone = 0;
  read_state.vio.vc_server = (VConnection *) this;

  Debug("muxvc", "[%d,%d] do_io_read for %d bytes", mux_vc->id, id, nbytes);

  if (other_side_closed & MUX_OCLOSE_INBOUND_MASK) {
    other_side_closed |= MUX_OCLOSE_NEED_READ_NOTIFY;
  }

  setup_retry_event(0);

  return &read_state.vio;
}

VIO *
MuxClientVC::do_io_write(Continuation * c, int64_t nbytes, IOBufferReader * abuffer, bool owner)
{

  ink_debug_assert(!closed);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);
  ink_debug_assert(owner == false);

  if (abuffer) {
    ink_assert(!owner);
    write_state.vio.buffer.reader_for(abuffer);
    write_state.enabled = 1;
  } else {
    write_state.vio.buffer.clear();
    write_state.enabled = 0;
  }

  write_state.vio.op = VIO::WRITE;
  write_state.vio.mutex = c->mutex;
  write_state.vio._cont = c;
  write_state.vio.nbytes = nbytes;
  write_state.vio.data = 0;
  write_state.vio.ndone = 0;
  write_state.vio.vc_server = (VConnection *) this;

  Debug("muxvc", "[%d,%d] do_io_write for %d bytes", mux_vc->id, id, nbytes);

  if (other_side_closed & MUX_OCLOSE_OUTBOUND_MASK) {
    other_side_closed |= MUX_OCLOSE_NEED_WRITE_NOTIFY;
  }

  setup_retry_event(0);

  return &write_state.vio;
}

void
MuxClientVC::reenable(VIO * vio)
{

  ink_debug_assert(!closed);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  Debug("muxvc", "[%d,%d] MuxClientVC::reenable %s", mux_vc->id, id, (vio->op == VIO::WRITE) ? "Write" : "Read");

  if (vio == &read_state.vio) {
    ink_debug_assert(vio->op == VIO::READ);
    read_state.enabled = 1;
  } else {
    ink_debug_assert(vio == &write_state.vio);
    ink_debug_assert(vio->op == VIO::WRITE);
    write_state.enabled = 1;
  }

  // We need to be running with MuxVC lock and
  //  on a different call stack so reschedule ourselves
  setup_retry_event(0);
}

void
MuxClientVC::reenable_re(VIO * vio)
{
  this->reenable(vio);
/*
    ink_debug_assert(!closed);
    ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

    Debug("muxvc", "[%d] reenable_re %s", id,
	  (vio->op == VIO::WRITE) ? "Write" : "Read");
	  */
}

void
MuxClientVC::boost()
{

  // We need to get lock for netVC to boost it
  MUTEX_TRY_LOCK(lock, mux_vc->mutex, this_ethread());
  if (lock) {
    if (mux_vc->net_vc) {
      mux_vc->net_vc->boost();
    }
  } else {
    need_boost = true;
    setup_retry_event(10);
  }
}

void
MuxClientVC::do_io_close(int flag)
{

  ink_debug_assert(closed == false);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  Debug("muxvc", "[%d, %d] do_io_close", mux_vc->id, id);

  closed = true;

  read_state.enabled = 0;
  read_state.vio.buffer.clear();
  read_state.vio.nbytes = 0;

  write_state.enabled = 0;
  write_state.vio.buffer.clear();
  write_state.vio.nbytes = 0;

  // If we get the do_io_close() on a callout, we
  //   must defer processing till the callout completes
  if (reentrancy_count != 0) {
    return;
  }
  // Now we need to try remove ourselves from the
  //   the parent MuxVC.
  MUTEX_TRY_LOCK(lock, mux_vc->mutex, this_ethread());
  if (lock) {
    mux_vc->remove_client(this);
  } else {
    setup_retry_event(10);
  }
}

// void MuxClientVC::do_io_shutdown(ShutdownHowTo_t howto)
//
//   With read side shutdown, we don't need to send
//     any control messages since a read shutdown indicates
//     we just need to discard data received
//
//   Write shutdowns reuqire us to inform the other side
//     that we are finished sending data so anyone doing
//     a read will get an EOS
//
//   See:  "UNIX Network Programming, Vol 1, Second
//           Edition" by Stevens, Section 6.6 for
//           a description of shutdown behavior with
//           regular sockets
//
void
MuxClientVC::do_io_shutdown(ShutdownHowTo_t howto)
{

  ink_debug_assert(closed == false);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  switch (howto) {
  case IO_SHUTDOWN_READ:
    read_state.shutdown = 1;
    read_state.enabled = 0;
    break;
  case IO_SHUTDOWN_READWRITE:
    read_state.shutdown = 1;
    read_state.enabled = 0;
    // FALL THROUGH
  case IO_SHUTDOWN_WRITE:
    {
      write_state.shutdown = (MUX_WRITE_SHUTDOWN | MUX_WRITE_SHUTUDOWN_SEND_MSG);
      write_state.enabled = 0;
      setup_retry_event(0);
      break;
    }
  }

  Debug("muxvc", "[%d,%d] do_io_shutdown %d", mux_vc->id, id, (int) howto);
}

void
MuxClientVC::set_active_timeout(ink_hrtime timeout_in)
{

  ink_debug_assert(closed == false);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  active_timeout = timeout_in;

  // FIX - Do we need to handle the case where the timeout is set
  //   but no io has been done?
  //
  // FIX - Locking?
  //
  if (active_event) {
    ink_assert(!active_event->cancelled);
    active_event->cancel();
    active_event = NULL;
  }

  if (active_timeout > 0) {
    active_event = eventProcessor.schedule_in(this, active_timeout);
  }
}

void
MuxClientVC::set_inactivity_timeout(ink_hrtime timeout_in)
{

  ink_debug_assert(closed == false);
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  inactive_timeout = timeout_in;

  // FIX - Do we need to handle the case where the timeout is set
  //   but no io has been done?
  //
  // FIX - Locking?
  //
  if (inactive_event) {
    ink_assert(!inactive_event->cancelled);
    inactive_event->cancel();
    inactive_event = NULL;
  }

  if (inactive_timeout > 0) {
    inactive_event = eventProcessor.schedule_in(this, inactive_timeout);
  }
}

void
MuxClientVC::cancel_active_timeout()
{
  set_active_timeout(0);
}

void
MuxClientVC::cancel_inactivity_timeout()
{
  set_inactivity_timeout(0);
}

ink_hrtime
MuxClientVC::get_active_timeout()
{
  return active_timeout;
}

ink_hrtime
MuxClientVC::get_inactivity_timeout()
{
  return inactive_timeout;
}

void
MuxClientVC::update_inactive_timeout()
{
  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  if (inactive_event) {
    inactive_event->cancel();
    inactive_event = eventProcessor.schedule_in(this, inactive_timeout);
  }
}

SOCKET
MuxClientVC::get_socket()
{
  return 0;
}

const struct sockaddr_in &
MuxClientVC::get_local_addr()
{
  return mux_vc->local_addr;
}

const struct sockaddr_in &
MuxClientVC::get_remote_addr()
{
  return mux_vc->remote_addr;
}

unsigned int
MuxClientVC::get_local_ip()
{
  return (unsigned int) get_local_addr().sin_addr.s_addr;
}

int
MuxClientVC::get_local_port()
{
  return ntohs(get_local_addr().sin_port);
}

unsigned int
MuxClientVC::get_remote_ip()
{
  return (unsigned int) get_remote_addr().sin_addr.s_addr;
}

int
MuxClientVC::get_remote_port()
{
  return ntohs(get_remote_addr().sin_port);;
}

int
MuxClientVC::main_handler(int event, void *data)
{

  Debug("muxvc", "[%d,%d] client main_hander %d 0x%x", mux_vc->id, id, event, data);

  ink_release_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);
  ink_release_assert(event == EVENT_INTERVAL || event == EVENT_IMMEDIATE);

  Event *calling_event = (Event *) data;
  EThread *my_ethread = this_ethread();
  bool read_mutex_held = false;
  bool write_mutex_held = false;
  Ptr<ProxyMutex> read_side_mutex = read_state.vio.mutex;
  Ptr<ProxyMutex> write_side_mutex = write_state.vio.mutex;

  ink_debug_assert(mutex->thread_holding == my_ethread);

  if (read_side_mutex) {
    read_mutex_held = MUTEX_TAKE_TRY_LOCK(read_side_mutex, my_ethread);

    if (!read_mutex_held) {
      calling_event->schedule_in(MUX_LOCK_RETRY);
      return 0;
    }
  }

  if (write_side_mutex) {
    write_mutex_held = MUTEX_TAKE_TRY_LOCK(write_side_mutex, my_ethread);

    if (!write_mutex_held) {
      if (read_mutex_held) {
        Mutex_unlock(read_side_mutex, my_ethread);
        calling_event->schedule_in(MUX_LOCK_RETRY);
        return 0;
      }
    }
  }
  // At this point, we hold the lock for MuxVC (since it's shares the lock with
  //   all it's MuxClientVC's and we've got the state machine locks for both sides
  //   of the connection so we should be have exclusive access all the data structures
  //   and be free from annoying and reenables, do_io calls, etc.
  if (data == active_event) {
    active_event = NULL;
    process_timeout(VC_EVENT_ACTIVE_TIMEOUT);
  } else if (data == inactive_event) {
    inactive_event = NULL;
    process_timeout(VC_EVENT_INACTIVITY_TIMEOUT);
  } else {
    ink_release_assert(data == retry_event);
    retry_event = NULL;
    process_retry_event();
  }

  if (read_mutex_held) {
    Mutex_unlock(read_side_mutex, my_ethread);
  }

  if (write_mutex_held) {
    Mutex_unlock(write_side_mutex, my_ethread);
  }

  return 0;
}


// void MuxClientVC::process_timeout(int event_to_send)
//
//     Sends timeouts.  All the locks were already taken
//        by main_handler
//
void
MuxClientVC::process_timeout(int event_to_send)
{

  ink_debug_assert(magic == MUX_VC_CLIENT_MAGIC_ALIVE);

  Debug("muxvc", "[%d,%d] process_timeout - event_to_send  %d", mux_vc->id, id, event_to_send);

  if (closed) {
    return;
  }

  if (read_state.vio.op == VIO::READ && !read_state.shutdown && read_state.vio.ntodo() > 0) {
    read_state.vio._cont->handleEvent(event_to_send, &read_state.vio);
  } else if (write_state.vio.op == VIO::WRITE && !write_state.shutdown && write_state.vio.ntodo() > 0) {
    write_state.vio._cont->handleEvent(event_to_send, &write_state.vio);
  }
}


// void MuxClientVC::setup_retry_event(int ms)
//
//   Sets up an event to this client for processing.  Retry events
//     can only be sent while holding the user sm's lock, pointed to by the
//     vios
//
void
MuxClientVC::setup_retry_event(int ms)
{
  if (!retry_event) {
    if (ms > 0) {
      retry_event = eventProcessor.schedule_in(this, HRTIME_MSECONDS(ms));
    } else {
      retry_event = eventProcessor.schedule_imm(this);
    }
  }
}

// void MuxClientVC::process_retry_event()
//
//   We've gotten this event because we missed a lock or needed to do something
//     on a different callstack.  We need to figure out our state and act
//     appropriately
//
void
MuxClientVC::process_retry_event()
{

  int bytes_written_to_mux = 0;
  Debug("muxvc", "[%d,%d] process_retry_event", mux_vc->id, id);

  if (closed) {
    // We missed the lock on the MuxVC during do_io_close().
    //   This time the callee has gotten the lock for us
    mux_vc->remove_client(this);
    return;
  }

  if (write_state.shutdown & MUX_WRITE_SHUTUDOWN_SEND_MSG) {
    bytes_written_to_mux += send_write_shutdown_message();
  }

  if (need_boost) {
    if (mux_vc->net_vc) {
      mux_vc->net_vc->boost();
    }
    need_boost = false;
  }

  if (read_state.enabled) {
    process_read_state();

    if (closed) {
      mux_vc->remove_client(this);
      return;
    }
  }

  if (write_state.enabled) {
    bytes_written_to_mux += process_write();

    if (closed) {
      mux_vc->remove_client(this);
      return;
    }
  }

  if (bytes_written_to_mux > 0) {
    if (mux_vc->write_vio && mux_vc->connect_state != MUX_CONNECTION_DROPPED) {
      mux_vc->write_vio->reenable();
    }
  }
}


void
MuxClientVC::process_read_state()
{

  ink_debug_assert(read_state.vio.mutex->thread_holding == this_ethread());
  ink_debug_assert(read_state.enabled);

  if (read_byte_bank) {
    this->process_byte_bank();
    if (closed) {
      return;
    }

    if (read_byte_bank) {
      Warning("Byte bank remains");
    }
  }
  // FIX - MUST ALSO CHECK FOR SHUTDOWN MSG
  if (other_side_closed & MUX_OCLOSE_INBOUND_MASK) {
    if (other_side_closed & MUX_OCLOSE_NEED_READ_NOTIFY && read_byte_bank == NULL) {
      this->process_channel_close_for_read();

      if (closed) {
        return;
      }
    }
  } else if (read_state.flow_stopped) {
    // If the client's buffer is not full & wants more bytes,
    //    unset flow control
    /*if (client->read_state.flow_stopped &&
       client->read_state.vio.ntodo() > 0 &&
       !client->read_state.vio.buffer.writer()->high_water()) {
       client->read_state.flow_stopped = 0;
       enqueue_control_message(INKMUX_MSG_FLOW_CONTROL_STOP, client->id);
       } */

  }
}

// void MuxClientVC::process_byte_bank()
//
//   Transfers bytes from the byte bank to the client
//      read buffer
//
//   CALLER must have take LOCK for client read side's
//     VIO
//
//   CALLER is responsible for handling reeentrany closes
//
int
MuxClientVC::process_byte_bank()
{

  int64_t bank_avail = byte_bank_reader->read_avail();
  int64_t vio_todo = read_state.vio.ntodotodo();
  int act_on = MIN(bank_avail, vio_todo);

  if (act_on > 0) {

    mux_move_data(read_state.vio.buffer.writer(), byte_bank_reader, act_on);

    bank_avail -= act_on;

    if (bank_avail == 0) {
      free_MIOBuffer(read_byte_bank);
      read_byte_bank = NULL;
      byte_bank_reader = NULL;
    }

    read_state.vio.ndone += act_on;

    int event;
    if (read_state.vio.ntodo() == 0) {
      event = VC_EVENT_READ_COMPLETE;
    } else {
      event = VC_EVENT_READ_READY;
    }

    reentrancy_count++;
    read_state.vio._cont->handleEvent(event, &read_state.vio);
    reentrancy_count--;
  }

  return bank_avail;
}


int
MuxClientVC::process_write()
{
  int bytes_written = 0;

  ink_debug_assert(write_state.vio.mutex->thread_holding == this_ethread());
  ink_debug_assert(write_state.enabled);

  if (other_side_closed & MUX_OCLOSE_OUTBOUND_MASK) {
    if (other_side_closed & MUX_OCLOSE_NEED_WRITE_NOTIFY) {
      process_channel_close_for_write();
    }
    return 0;
  } else {

    ink_debug_assert(!closed);

    int64_t ntodo = write_state.vio.ntodo();
    if (ntodo == 0 || write_state.shutdown) {
      write_state.enabled = 0;
      return 0;
    }

    int avail = write_state.vio.buffer.reader()->read_avail();
    int act_on = MIN(ntodo, avail);

    // FIX ME - if we don't send all the data in the buffer
    //   the connection can lock up because the producer
    //   expects all the data to be removed from the buffer
    //   if the watermark is 0
    //
    //   But by not limiting it, we allow one transaction
    //     to starve others
    //
    // act_on = MIN(act_on, MUX_MAX_BYTES_SLOT);

    ink_debug_assert(act_on >= 0);
    if (act_on <= 0) {
      Debug("muxvc", "[process_write] disabling [%d,%d]" " due to zero bytes", mux_vc->id, id);
      write_state.enabled = 0;

      // Notify the client we're disabling it due to lack of data
      reentrancy_count++;
      write_state.vio._cont->handleEvent(VC_EVENT_WRITE_READY, &write_state.vio);
      reentrancy_count--;

      return 0;
    }
    // If we've got too much data outstanding in the write buffer,
    //   don't add any more
    if (mux_vc->write_high_water()) {
      mux_vc->writes_blocked = true;
      return 0;
    }

    int left = act_on;

    while (left > 0) {
      int msg_bytes = MIN(left, MUX_MAX_DATA_SIZE);
      bytes_written += mux_vc->enqueue_control_message(INKMUX_MSG_NORMAL_DATA, id, msg_bytes);
      mux_move_data(mux_vc->write_buffer, write_state.vio.buffer.reader(), msg_bytes);
      left -= msg_bytes;
    }

    write_state.vio.ndone += act_on;
    update_inactive_timeout();

    Debug("muxvc", "[process_write] callback for [%d,%d]"
          " ndone %d, nbytes %d", mux_vc->id, id, write_state.vio.ndone, write_state.vio.nbytes);

    int event;
    if (write_state.vio.ntodo() == 0) {
      write_state.enabled = 0;
      event = VC_EVENT_WRITE_COMPLETE;
    } else {
      event = VC_EVENT_WRITE_READY;
    }

    reentrancy_count++;
    write_state.vio._cont->handleEvent(event, &write_state.vio);
    reentrancy_count--;

  }

  // FIX - WHAT ABOUT PENDING SHUTDOWN MESSAGE!

  return bytes_written;
}


// void MuxClientVC::process_channel_close_for_read()
//
//    Handles sending EOS to the read side of the client
//      when the the remote side closes the channel
//
//    CALLER is responsible for handling reentrant closes
//
void
MuxClientVC::process_channel_close_for_read()
{

  ink_debug_assert(!closed);
  ink_debug_assert(other_side_closed & MUX_OCLOSE_NEED_READ_NOTIFY);
  ink_debug_assert(read_state.vio.mutex->thread_holding == this_ethread());
  ink_debug_assert(read_byte_bank == NULL);

  if (!read_state.shutdown && read_state.vio.ntodo() > 0) {

    other_side_closed &= ~MUX_OCLOSE_NEED_READ_NOTIFY;

    reentrancy_count++;
    read_state.vio._cont->handleEvent(VC_EVENT_EOS, &read_state.vio);
    reentrancy_count--;
  }
}

// void MuxClientVC::process_channel_close_for_write()
//
//    Handles sending ERROR to the write side of the client
//      when the the remote side closes the channel
//
//    CALLER is responsible for handling reentrant closes
//
void
MuxClientVC::process_channel_close_for_write()
{

  ink_debug_assert(!closed);
  ink_debug_assert(other_side_closed & MUX_OCLOSE_NEED_WRITE_NOTIFY);
  ink_debug_assert(write_state.vio.mutex->thread_holding == this_ethread());

  if (!write_state.shutdown && write_state.vio.ntodo() > 0) {

    other_side_closed &= ~MUX_OCLOSE_NEED_WRITE_NOTIFY;

    reentrancy_count++;
    write_state.vio._cont->handleEvent(VC_EVENT_ERROR, &write_state.vio);
    reentrancy_count--;
  }
}

int
MuxClientVC::send_write_shutdown_message()
{

  ink_debug_assert(!closed);
  ink_debug_assert(write_state.shutdown & MUX_WRITE_SHUTUDOWN_SEND_MSG);
  ink_debug_assert(this->mutex->thread_holding == this_ethread());

  write_state.shutdown &= ~MUX_WRITE_SHUTUDOWN_SEND_MSG;

  return mux_vc->enqueue_control_message(INKMUX_MSG_SHUTDOWN_WRITE, id, 0);
}


static int next_muxvc_id = 0;

MuxVC::MuxVC():
magic(MUX_VC_MAGIC_ALIVE),
id(0),
reentrancy_count(0),
terminate_vc(false),
on_mux_list(false),
clients_notified_of_error(false),
process_event(NULL),
net_vc(NULL),
read_vio(NULL),
write_vio(NULL),
write_bytes_added(0),
writes_blocked(false),
net_connect_action(NULL),
return_connect_action(),
connect_state(MUX_NOT_CONNECTED),
retry_event(NULL),
read_buffer(NULL),
write_buffer(NULL),
read_buffer_reader(NULL),
read_msg_state(MUX_READ_MSG_HEADER),
read_msg_size(0),
read_msg_ndone(0),
current_msg_hdr(), discard_read_data(false), return_accept_action(), next_client_id(1), num_clients(0), active_clients()
{

  memset(&local_addr, 0, sizeof(sockaddr_in));
  memset(&remote_addr, 0, sizeof(sockaddr_in));
}

void
MuxVC::init()
{
  mutex = new_ProxyMutex();
  id = ink_atomic_increment(&next_muxvc_id, 1);

  Debug("mux_alloc", "[%d] Created new MuxVC", id);
}

void
MuxVC::init_from_accept(NetVConnection * nvc, Continuation * acceptc)
{
  mutex = new_ProxyMutex();
  net_vc = nvc;
  connect_state = MUX_CONNECTED_ACTIVE;
  set_mux_accept(acceptc);

  init_buffers();

  id = ink_atomic_increment(&next_muxvc_id, 1);
  Debug("mux_alloc", "[%d] Created new MuxVC from accept", id);

  MUTEX_TAKE_LOCK(mutex, this_ethread());
  init_io();
  Mutex_unlock(mutex, mutex->thread_holding);

}

void
MuxVC::init_buffers()
{

  if (!read_buffer) {
    read_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    ink_debug_assert(read_buffer_reader == NULL);
    read_buffer_reader = read_buffer->alloc_reader();
  }

  if (!write_buffer) {
    write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
  }
}

void
MuxVC::init_io()
{
  SET_HANDLER(MuxVC::state_handle_mux);
  read_vio = net_vc->do_io_read(this, INT64_MAX, read_buffer);
  write_vio = net_vc->do_io_write(this, INT64_MAX, write_buffer->alloc_reader());
}


MuxVC::~MuxVC()
{
  magic = MUX_VC_MAGIC_DEAD;
}

bool
MuxVC::on_list(MuxClientVC * c)
{
  MuxClientVC *tmp = active_clients.head;

  while (tmp) {
    if (c == tmp) {
      return true;
    }
    tmp = tmp->link.next;
  }
  return false;
}

Action *
MuxVC::do_connect(Continuation * c, unsigned int ip, int port)
{

  ink_debug_assert(magic == MUX_VC_MAGIC_ALIVE);
  ink_debug_assert(return_connect_action.continuation == NULL);
  ink_debug_assert(connect_state == MUX_NOT_CONNECTED);

  reentrancy_count++;
  connect_state = MUX_NET_CONNECT_ISSUED;

  return_connect_action = c;
  SET_HANDLER(MuxVC::state_handle_connect);

  Debug("muxvc", "MuxVC::do_connect issued to %u.%u.%u.%u port %d",
        ((unsigned char *) &ip)[0],
        ((unsigned char *) &ip)[1], ((unsigned char *) &ip)[2], ((unsigned char *) &ip)[3], port);

  // Keep our own mutex ref as we can get deallocted on the
  //   on the callback
  ProxyMutexPtr my_mutex_ref = mutex;

  // Fix Me: need to respect interface binding
  MUTEX_TAKE_LOCK(my_mutex_ref, this_ethread());
  Action *tmp = netProcessor.connect_re(this, ip, port);
  Mutex_unlock(my_mutex_ref, my_mutex_ref->thread_holding);

  if (tmp != ACTION_RESULT_DONE) {
    net_connect_action = tmp;
  }

  Debug("mux_open", "do_connect state is %d", connect_state);
  reentrancy_count--;

  switch (connect_state) {
  case MUX_NET_CONNECT_ISSUED:
    return &return_connect_action;
  case MUX_WAIT_FOR_READY:
    return &return_connect_action;
  case MUX_CONNECT_FAILED:
    kill();
    return ACTION_RESULT_DONE;
  default:
    ink_release_assert(0);
    break;
  }

  return NULL;
}

int
MuxVC::state_handle_connect(int event, void *data)
{

  ink_release_assert(magic == MUX_VC_MAGIC_ALIVE);
  ink_debug_assert(net_vc == NULL);

  Debug("muxvc", "MuxVC::connect_handler event %d", event);
  Debug("mux_open", "MuxVC::connect_handler event %d", event);
  net_connect_action = NULL;

  switch (event) {
  case NET_EVENT_OPEN:
    // FIX - Unix & NT connect behavior differ
    connect_state = MUX_WAIT_FOR_READY;
    net_vc = (NetVConnection *) data;
    setup_connect_check();
    break;
  case NET_EVENT_OPEN_FAILED:
    connect_state = MUX_CONNECT_FAILED;
    state_send_init_response(EVENT_NONE, NULL);
    break;
  default:
    ink_release_assert(0);
    break;
  }

  return 0;
}

// int MuxVC::state_wait_for_ready(int event, void* data)
//
//   Checks to see if a socket goes ready or timesout
//     after a connect
//
int
MuxVC::state_wait_for_ready(int event, void *data)
{

  ink_release_assert(magic == MUX_VC_MAGIC_ALIVE);
  ink_debug_assert(connect_state == MUX_WAIT_FOR_READY);

  Debug("muxvc", "MuxVC::state_wait_for_ready event %d", event);
  Debug("mux_open", "MuxVC::state_wait_for_ready event %d", event);

  SET_HANDLER(MuxVC::state_send_init_response);

  switch (event) {
  case VC_EVENT_WRITE_READY:
    ink_debug_assert(data == write_vio);
    connect_state = MUX_CONNECTED_ACTIVE;
    net_vc->cancel_inactivity_timeout();
    net_vc->do_io_write(this, 0, NULL);
    local_addr = net_vc->get_local_addr();
    remote_addr = net_vc->get_remote_addr();
    write_vio = NULL;
    state_send_init_response(EVENT_NONE, NULL);
    break;
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ERROR:
    connect_state = MUX_CONNECT_FAILED;
    net_vc->do_io_close(0);
    net_vc = NULL;
    state_send_init_response(EVENT_NONE, NULL);
    break;
  default:
    ink_release_assert(0);
    break;
  }

  return 0;
}

// int MuxVC::state_send_init_response(int event, void* data)
//
//    sends an event in response to the do_connect() call
//
int
MuxVC::state_send_init_response(int event, void *data)
{

  ink_debug_assert(event == EVENT_NONE || (event == EVENT_INTERVAL && data == retry_event));

  if (event == EVENT_INTERVAL) {
    retry_event = NULL;
  }

  MUTEX_TRY_LOCK(lock, return_connect_action.mutex, this_ethread());

  if (!lock) {
    Debug("mux_open", "[MuxVC::state_send_init_response] lock missed, retrying");
    retry_event = eventProcessor.schedule_in(this, MUX_LOCK_RETRY);
    return 0;
  }

  if (!return_connect_action.cancelled) {
    Continuation *callback_c = return_connect_action.continuation;
    return_connect_action = NULL;

    switch (connect_state) {
    case MUX_CONNECTED_ACTIVE:
      Debug("mux_open", "[MuxVC::state_send_init_response] sending MUX_EVENT_OPEN");
      callback_c->handleEvent(MUX_EVENT_OPEN);
      init_buffers();
      init_io();
      return_connect_action = NULL;
      break;
    case MUX_CONNECT_FAILED:
      Debug("mux_open", "[MuxVC::state_send_init_response] sending MUX_EVENT_FAILED");
      callback_c->handleEvent(MUX_EVENT_OPEN_FAILED);

      // We doing lazy reentrancy counting.  Only doing
      //   where we know there are issues.  So if the count
      //   is zero no one is blocking us from deallocating
      //   ourselves
      if (reentrancy_count == 0) {
        kill();
      }
      break;
    default:
      ink_release_assert(0);
      break;

    }

  } else {
    return_connect_action = NULL;
    kill();
  }

  return 0;
}

// void MuxVC:::setup_connect_check()
//
//  On Unix platforms, connect is non-blocking and doesn't acutally tell
//    you if the connect succeeded.  We need to set up a write and wait
//    for write ready to see if the connect actually worked
//
void
MuxVC::setup_connect_check()
{

  write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_1K);
  IOBufferReader *r = write_buffer->alloc_reader();

  SET_HANDLER(MuxVC::state_wait_for_ready);

  // FIX - ready timeout should be tunable
  net_vc->set_inactivity_timeout(HRTIME_SECONDS(30));

  ink_debug_assert(write_vio == NULL);
  write_vio = net_vc->do_io_write(this, INT64_MAX, r);
}


Action *
MuxVC::set_mux_accept(Continuation * c)
{
  return_accept_action = c;
  return &return_accept_action;
}

void
MuxVC::kill()
{

  ink_debug_assert(mutex->thread_holding == this_ethread());
  ink_debug_assert(reentrancy_count == 0);
  ink_release_assert(num_clients == 0);

  Debug("mux_alloc", "[%d] Cleaning up MuxVC", id);

  magic = MUX_VC_MAGIC_DEAD;

  if (net_vc) {
    net_vc->do_io_close(0);
    net_vc = NULL;
  }

  if (net_connect_action) {
    net_connect_action->cancel();
    net_connect_action = NULL;
  }

  return_connect_action = NULL;

  if (read_buffer) {
    free_MIOBuffer(read_buffer);
    read_buffer = NULL;
  }

  if (write_buffer) {
    free_MIOBuffer(write_buffer);
    write_buffer = NULL;
  }

  if (process_event != NULL) {
    process_event->cancel();
    process_event = NULL;
  }
  // If we are on the mux processor list, we must remove ourself
  //    before we can dealloc ourself
  if (on_mux_list) {
    if (try_processor_list_remove() == 0) {
      SET_HANDLER(MuxVC::state_remove_from_list);
      setup_process_event(10);
      return;
    }
  }

  Debug("mux_alloc", "[%d] Killing MuxVC", id);
  ink_debug_assert(on_mux_list == false);
  mutex = NULL;
  delete this;
}

int
MuxVC::state_remove_from_list(int event, void *data)
{

  ink_debug_assert(data == process_event);
  ink_debug_assert(event == EVENT_INTERVAL);
  ink_debug_assert(on_mux_list);

  process_event = NULL;
  if (try_processor_list_remove() == 0) {
    setup_process_event(10);
  } else {
    kill();
  }

  return EVENT_DONE;
}

int
MuxVC::try_processor_list_remove()
{

  MUTEX_TRY_LOCK(list_lock, muxProcessor.list_mutex, this_ethread());
  if (list_lock) {
    muxProcessor.mux_list.remove(this);
    on_mux_list = false;
    return 1;
  }

  return 0;
}

// MuxClientVC* MuxVC::new_client(int32_t id_arg)
//
//   Caller MUST be holding MuxVC::mutex
//
MuxClientVC *
MuxVC::new_client(int32_t id_arg)
{

  ink_debug_assert(magic == MUX_VC_MAGIC_ALIVE);
  ink_release_assert(mutex->thread_holding == this_ethread());

  if (connect_state == MUX_CONNECTED_IDLE) {
    ink_debug_assert(process_event != NULL);
    process_event->cancel();
    process_event = NULL;
    connect_state = MUX_CONNECTED_ACTIVE;
    SET_HANDLER(MuxVC::state_handle_mux);
  }
  ink_debug_assert(connect_state == MUX_CONNECTED_ACTIVE);

  MuxClientVC *new_client = NEW(new MuxClientVC);

  if (id_arg == 0) {
    id_arg = next_client_id++;
    enqueue_control_message(INKMUX_MSG_OPEN_CHANNEL, id_arg, 0);
  }

  Debug("muxvc", "creating new client with id %d", id_arg);
  Debug("mux_alloc", "[%d,%d] Creating new mux client id", this->id, id_arg);

  new_client->init(this, id_arg);

  num_clients++;
  active_clients.push(new_client);

  ink_debug_assert(on_list(new_client) == true);

  return new_client;
}

// void MuxVC::remove_client(MuxClientVC* client)
//
//    Callee must be holding this->mutex
//
void
MuxVC::remove_client(MuxClientVC * client)
{

  ink_debug_assert(mutex->thread_holding == this_ethread());

  num_clients--;
  active_clients.remove(client);

  if (!client->other_side_closed & MUX_OCLOSE_CHANNEL_EVENT) {
    enqueue_control_message(INKMUX_MSG_CLOSE_CHANNEL, client->id, 0);
  }

  Debug("mux_alloc", "[%d,%d] Remvoing mux client id", this->id, client->id);
  client->kill();

  // If we're out of clients, we either need to go into an idle state
  //    or kill ourselves
  if (num_clients == 0) {
    switch (connect_state) {
    case MUX_CONNECTED_ACTIVE:
      Debug("muxvc", "[%d] Setting muxVC to idle state", id);
      connect_state = MUX_CONNECTED_IDLE;
      SET_HANDLER(MuxVC::state_idle);
      if (process_event) {
        process_event->cancel();
        process_event = NULL;
      }
      setup_process_event(60000);       // Fix me - make configurable
      break;
    case MUX_CONNECTION_DROPPED:
      if (reentrancy_count == 0) {
        kill();
      } else {
        terminate_vc = true;
      }
      break;
    default:
      ink_release_assert(0);
      break;
    }
  }
}

// void MuxVC::enqueue_control_message(int msg_id, int32_t cid, int data_size)
//
//   Builds a control message and inserts it on the write buffer
//
int
MuxVC::enqueue_control_message(int msg_id, int32_t cid, int data_size)
{

  ink_debug_assert(data_size + sizeof(MuxMessage) <= USHRT_MAX);

  MuxMessage mm;
  Debug("mux_cntl", "enqueue_control_message: %s for %d", control_msg_id_to_string(msg_id), cid);

  mm.version = (uint8_t) INKMUX_PROTO_VERSION_0_1;
  mm.msg_type = (uint8_t) msg_id;
  mm.msg_len = (uint16_t) (sizeof(MuxMessage) + data_size);
  mm.client_id = cid;

  write_buffer->write((char *) &mm, sizeof(MuxMessage));

  if (write_vio && connect_state != MUX_CONNECTION_DROPPED) {
    write_vio->reenable();
  }

  write_bytes_added += mm.msg_len;

  return (int) mm.msg_len;
};


void
MuxVC::process_clients()
{

  ink_debug_assert(magic == MUX_VC_MAGIC_ALIVE);

  EThread *my_ethread = this_ethread();
  MuxClientVC *current = active_clients.head;
  MuxClientVC *next = NULL;

  int locks_missed = 0;
  int bytes_written = 0;
  int count = 0;

  for (; current != NULL; current = next) {

    count++;
    next = current->link.next;

    if (current->closed) {
      this->remove_client(current);
      continue;
    }

    if (current->write_state.enabled) {
      MUTEX_TRY_LOCK(wlock, current->write_state.vio.mutex, my_ethread);

      if (wlock) {
        if (current->write_state.enabled) {
          bytes_written += current->process_write();

          if (current->closed) {
            this->remove_client(current);
            continue;
          }
        }
      } else {
        locks_missed++;
      }
    }

    if (current->read_state.enabled) {
      MUTEX_TRY_LOCK(rlock, current->read_state.vio.mutex, my_ethread);

      if (rlock) {
        if (current->read_state.enabled) {
          current->process_read_state();

          if (current->closed) {
            this->remove_client(current);
            continue;
          }
        }
      } else {
        locks_missed++;
      }
    }
  }

  if (bytes_written > 0) {
    Debug("muxvc", "MuxVC::process_clients - reenabling write, %d bytes added", bytes_written);
    write_vio->reenable();
  }

  if (locks_missed > 0) {
    setup_process_event(10);
  }
}

// MuxClientVC* MuxVC::find_client(int32_t client_id)
//
//   Search the client list of a MuxClientVC
//    matching clinet_id
//
MuxClientVC *
MuxVC::find_client(int32_t client_id)
{

  MuxClientVC *current = active_clients.head;

  while (current) {
    if (current->id == client_id) {
      return current;
    }
    current = current->link.next;
  }

  return NULL;
}

// void MuxVC::process_read_msg_body()
//
//   Process the body of a data message to put the data on
//     the client vc
//
void
MuxVC::process_read_msg_body()
{

  bool need_byte_bank = false;
  bool need_flow_control = false;
  MuxClientVC *client = NULL;

  ink_debug_assert(read_msg_state == MUX_READ_MSG_BODY);

  int avail = read_buffer_reader->read_avail();
  if (avail > 0) {

    if (!discard_read_data) {
      client = find_client(current_msg_hdr.client_id);

      if (!client) {
        // No client - send a reset message to remote side
        discard_read_data = true;
        enqueue_control_message(INKMUX_MSG_CHANNEL_RESET, current_msg_hdr.client_id);
      } else {

        if (client->read_state.vio.op != VIO::READ || !client->read_state.vio.mutex) {
          // No active read
          need_byte_bank = need_flow_control = true;
          goto ADD_TO_BYTE_BANK;
        }


        MUTEX_TRY_LOCK(lock, client->read_state.vio.mutex, this_ethread());
        if (lock) {

          if (client->closed) {
            discard_read_data = true;
            enqueue_control_message(INKMUX_MSG_CHANNEL_RESET, current_msg_hdr.client_id);
          } else if (client->read_state.shutdown) {
            discard_read_data = true;
          } else {

            // Process the outstanding byte bank
            if (client->read_byte_bank) {
              int res = client->process_byte_bank();
              if (client->closed) {
                this->remove_client(client);
                return;
              } else if (res > 0) {
                // If there is still data on the byte bank,
                //   all new data must go to the byte bank
                //   as well
                need_byte_bank = true;
                goto ADD_TO_BYTE_BANK;
              }
            }
            // We've got the lock and the clinet is not closed
            //   or shutdown.  See how much data we can shove on the
            //   client's buffer
            int left_in_msg = current_msg_hdr.msg_len - read_msg_ndone;
            int act_on = MIN(avail, left_in_msg);
            int64_t vio_todo = client->read_state.vio.ntodo();

            // If available bytes execeeds amount the I/O requested
            //   on the local side, tell the other side to stop sending
            if (act_on > vio_todo) {
              need_byte_bank = true;
              act_on = vio_todo;
              need_flow_control = true;
            }
            // User has set nbytes to ndone so we have
            //   don't have any data to more
            if (vio_todo == 0) {
              client->read_state.enabled = 0;
              goto ADD_TO_BYTE_BANK;
            }

            Debug("muxvc", "reading %d bytes of %d for %d",
                  act_on, (int) current_msg_hdr.msg_len, current_msg_hdr.client_id);

            mux_move_data(client->read_state.vio.buffer.writer(), read_buffer_reader, act_on);

            client->read_state.vio.ndone += act_on;
            read_msg_ndone += act_on;

            int event;
            if (client->read_state.vio.ntodo() == 0) {
              event = VC_EVENT_READ_COMPLETE;
            } else {
              event = VC_EVENT_READ_READY;

              MIOBuffer *client_buf = client->read_state.vio.buffer.writer();
              if (client_buf->high_water() && client_buf->max_read_avail() >= client_buf->block_size()) {
                need_flow_control = 1;
              }
            }
            Debug("muxvc", "[MuxVC::process_read_msg_body] callback for [%d,%d]"
                  " ndone %d, nbytes %d", id, current_msg_hdr.client_id,
                  client->read_state.vio.ndone, client->read_state.vio.nbytes);

            client->update_inactive_timeout();
            client->reentrancy_count++;
            client->read_state.vio._cont->handleEvent(event, &client->read_state.vio);
            client->reentrancy_count--;

            if (client->closed) {
              this->remove_client(client);
              return;
            }
          }
        } else {
          need_byte_bank = true;
        }
      }
    }
    // If the client isn't available or has closed or shutdown reading,
    //    discard the input data
    if (discard_read_data) {
      ink_debug_assert(need_byte_bank == false);
      int left_in_msg = current_msg_hdr.msg_len - read_msg_ndone;
      int act_on = MIN(avail, left_in_msg);
      read_buffer_reader->consume(act_on);
      read_msg_ndone += act_on;
    }

  ADD_TO_BYTE_BANK:
    if (need_byte_bank) {
      ink_debug_assert(discard_read_data == false);

      // Either missed the lock or bytes sent exceeds the amount the client asked
      //  for.  Need to store in byte until client is ready for these bytes
      if (client->read_byte_bank == NULL) {
        client->read_byte_bank = new_MIOBuffer(BUFFER_SIZE_INDEX_1K);
        client->byte_bank_reader = client->read_byte_bank->alloc_reader();

      }
      // Above operations could have changed avail so get
      //  fresh info
      avail = read_buffer_reader->read_avail();
      int left_in_msg = current_msg_hdr.msg_len - read_msg_ndone;
      int act_on = MIN(avail, left_in_msg);

      Debug("muxvc", "adding %d bytes to byte bank for [%d,%d]", act_on, id, current_msg_hdr.client_id);
      Debug("mux_bank", "adding %d bytes to byte bank for [%d,%d]", act_on, id, current_msg_hdr.client_id);

      mux_move_data(client->read_byte_bank, read_buffer_reader, act_on);
      read_msg_ndone += act_on;

      if (client->byte_bank_reader->read_avail() > MUX_MAX_BYTES_BANK) {
        need_flow_control = true;
      }
      setup_process_event(10);
    }

    /*if (need_flow_control) {
       enqueue_control_message(INKMUX_MSG_FLOW_CONTROL_START,
       current_msg_hdr.client_id);
       client->read_state.flow_stopped = 1;
       client->read_state.enabled = 0;
       }
     */

    if (read_msg_ndone == current_msg_hdr.msg_len) {
      Debug("muxvc", "completed read of normal data for id %d len %d",
            current_msg_hdr.client_id, (int) current_msg_hdr.msg_len);
      reset_read_msg_state();
    }
  }
}

// void MuxVC::process_read_data()
//
//   Loops over input stream and process messages
//
void
MuxVC::process_read_data()
{

  while (read_buffer_reader->read_avail() > 0) {

    if (read_msg_state == MUX_READ_MSG_HEADER) {
      char *copy_to = ((char *) (&current_msg_hdr)) + read_msg_ndone;
      int act_on = sizeof(MuxMessage) - read_msg_ndone;
      ink_debug_assert(act_on > 0);

      int res = read_buffer_reader->read(copy_to, act_on);
      read_msg_ndone += res;

      if (read_msg_ndone == sizeof(MuxMessage)) {

        if (current_msg_hdr.msg_type != INKMUX_MSG_NORMAL_DATA) {

          process_control_message();
          reset_read_msg_state();
        } else {

          // Check for bogus zero body length
          if (current_msg_hdr.msg_len == read_msg_ndone) {
            reset_read_msg_state();
            continue;
          }

          read_msg_state = MUX_READ_MSG_BODY;
        }
      }
    }

    if (read_msg_state == MUX_READ_MSG_BODY) {
      Debug("muxvc", "control msg - normal data for %d len %d",
            current_msg_hdr.client_id, (int) current_msg_hdr.msg_len);
      process_read_msg_body();
    }
  }
}

void
MuxVC::process_control_message()
{

  MuxClientVC *client = find_client(current_msg_hdr.client_id);

  int msg_type = (int) current_msg_hdr.msg_type;

  Debug("mux_cntl", "control msg %s for %d", control_msg_id_to_string(msg_type), current_msg_hdr.client_id);

  switch (msg_type) {
  case INKMUX_MSG_OPEN_CHANNEL:
    process_channel_open();
    break;
  case INKMUX_MSG_CLOSE_CHANNEL:
    {
      if (client) {
        client->other_side_closed |=
          (MUX_OCLOSE_CHANNEL_EVENT | MUX_OCLOSE_NEED_READ_NOTIFY | MUX_OCLOSE_NEED_WRITE_NOTIFY);
        process_channel_close(client);

        if (client->closed) {
          remove_client(client);
        }
      }
      break;
    }
  case INKMUX_MSG_CHANNEL_RESET:
    break;
  case INKMUX_MSG_FLOW_CONTROL_START:
    client->write_state.flow_stopped = 1;
    break;
  case INKMUX_MSG_FLOW_CONTROL_STOP:
    client->write_state.flow_stopped = 0;
    process_clients();
    break;
  case INKMUX_MSG_SHUTDOWN_WRITE:
    if (client) {
      client->other_side_closed |= (MUX_OCLOSE_WRITE_EVENT | MUX_OCLOSE_NEED_READ_NOTIFY);
      process_channel_inbound_shutdown(client);

      if (client->closed) {
        remove_client(client);
      }
    }
    break;

  default:
    ink_release_assert(0);
  }

}

void
MuxVC::process_channel_open()
{

  if (!return_accept_action.continuation) {
    enqueue_control_message(INKMUX_MSG_CLOSE_CHANNEL, current_msg_hdr.client_id, 0);
    return;
  }
  // Right now, only the initiating side can create sessions due to
  //   how the id's are managed.  If we're receiving a session, we
  //   could not have ever created one
  ink_release_assert(next_client_id == 1);

  MuxClientVC *new_vc = new_client(current_msg_hdr.client_id);


  EThread *my_ethread = this_ethread();

  // FIX - nasty hack - need to understand why we need netvc->thread ptr
  new_vc->thread = my_ethread;

  // FIX - should try locks and retries here
  //  Fix - need check if we have a mutex before taking it
//    MUTEX_TAKE_LOCK(return_accept_action.mutex, my_ethread);

  if (!return_accept_action.cancelled) {

    return_accept_action.continuation->handleEvent(NET_EVENT_ACCEPT, new_vc);
  }
//    Mutex_unlock(return_accept_action.mutex, my_ethread);
}

// void MuxVC::process_channel_close(MuxClientVC* client)
//
//    Handles sending EOS & ERROR events to the client when the
//      the other side closed the channel
//
//    CALLER is responsible for handling reentrant closes
//
void
MuxVC::process_channel_close(MuxClientVC * client)
{

  EThread *my_ethread = this_ethread();

  if (client->other_side_closed & MUX_OCLOSE_NEED_READ_NOTIFY) {

    if (client->read_state.vio.mutex) {
      MUTEX_TRY_LOCK(rlock, client->read_state.vio.mutex, my_ethread);

      if (rlock) {
        if (                    //client->read_state.enabled &&
             !client->closed && !client->read_byte_bank) {
          // If the read is active and there's no byte bank,
          //   notify of the close.  We need to defer on a byte
          //   bank as that data needs to be processed before shutting
          //   down the channel
          client->process_channel_close_for_read();

          // If the client closed on the callback, bail
          if (client->closed) {
            return;
          }
        }
      } else {
        setup_process_event(10);
      }
    } else {
      client->other_side_closed &= ~MUX_OCLOSE_NEED_READ_NOTIFY;
    }
  }

  if (client->other_side_closed & MUX_OCLOSE_NEED_WRITE_NOTIFY) {

    if (client->write_state.vio.mutex) {
      MUTEX_TRY_LOCK(wlock, client->write_state.vio.mutex, my_ethread);

      if (wlock) {
        if (                    //client->write_state.enabled &&
             !client->closed) {
          client->process_channel_close_for_write();

          // If the client closed on the callback, bail
          if (client->closed) {
            return;
          }
        }

      } else {
        setup_process_event(10);
      }
    } else {
      client->other_side_closed &= ~MUX_OCLOSE_NEED_WRITE_NOTIFY;
    }
  }
}


void
MuxVC::process_channel_inbound_shutdown(MuxClientVC * client)
{

  EThread *my_ethread = this_ethread();

  ink_debug_assert(client->other_side_closed & MUX_OCLOSE_NEED_READ_NOTIFY);

  if (client->read_state.vio.mutex) {
    MUTEX_TRY_LOCK(rlock, client->read_state.vio.mutex, my_ethread);

    if (rlock) {
      if (client->read_state.enabled && !client->closed && !client->read_byte_bank) {
        // If the read is active and there's no byte bank,
        //   notify of the close.  We need to defer on a byte
        //   bank as that data needs to be processed before shutting
        //   down the channel
        client->process_channel_close_for_read();

        // If the client closed on the callback, bail
        if (client->closed) {
          return;
        }
      }
    } else {
      setup_process_event(10);
    }
  } else {
    client->other_side_closed &= ~MUX_OCLOSE_NEED_READ_NOTIFY;
  }
}

void
MuxVC::reset_read_msg_state()
{
  read_msg_state = MUX_READ_MSG_HEADER;
  read_msg_size = 0;
  read_msg_ndone = 0;
  discard_read_data = false;

  memset(&current_msg_hdr, 0, sizeof(MuxMessage));
}

void
MuxVC::setup_process_event(int ms)
{
  if (!process_event) {
    if (ms > 0) {
      process_event = eventProcessor.schedule_in(this, HRTIME_MSECONDS(ms));
    } else {
      process_event = eventProcessor.schedule_imm(this);
    }
  }
}

bool
MuxVC::write_high_water()
{
  if (write_bytes_added - write_vio->ndone > MUX_WRITE_HIGH_WATER && 0) {
    return true;
  } else {
    return false;
  }
}

void
MuxVC::cleanup_on_error()
{

  ink_debug_assert(connect_state == MUX_CONNECTION_DROPPED);

  MuxClientVC *current = active_clients.head;
  MuxClientVC *next = NULL;

  reentrancy_count++;

  Debug("muxvc", "[MuxVC::cleanup_on_error] for %d", id);

  if (num_clients == 0) {
    terminate_vc = true;
  } else {
    if (clients_notified_of_error == false) {
      // Loop over the clients trying to kill them off
      for (; current != NULL; current = next) {

        next = current->link.next;

        if (!current->closed) {

          // Take care not to renotify if notification has already
          //   received
          if (!(current->other_side_closed & MUX_OCLOSE_INBOUND_MASK)) {
            current->other_side_closed |= MUX_OCLOSE_NEED_READ_NOTIFY;
          }

          if (!(current->other_side_closed & MUX_OCLOSE_OUTBOUND_MASK)) {
            current->other_side_closed |= MUX_OCLOSE_NEED_WRITE_NOTIFY;
          }

          current->other_side_closed |= MUX_OCLOSE_CHANNEL_EVENT;
          process_channel_close(current);
        }

        if (current->closed) {
          remove_client(current);
        }
      }
      clients_notified_of_error = true;
    }
  }

  reentrancy_count--;
  if (terminate_vc && reentrancy_count == 0) {
    kill();
  }
}

// int MuxVC::state_teardown(int event, void* data)
//
//   We're waiting for everything in the write
//      buffer to be sent
//
int
MuxVC::state_teardown(int event, void *data)
{

  Debug("muxvc", "state_teardown: event %d", event);
  reentrancy_count++;

  switch (event) {
  case VC_EVENT_WRITE_COMPLETE:
    ink_debug_assert(data == write_vio);
    terminate_vc = true;
    break;
  case VC_EVENT_WRITE_READY:
    // Ignore
    break;
  default:
    ink_release_assert(0);
  }

  reentrancy_count--;
  if (terminate_vc && reentrancy_count == 0) {
    kill();
  }

  return EVENT_DONE;
}

int
MuxVC::state_idle(int event, void *data)
{

  int r = EVENT_DONE;

  ink_release_assert(magic == MUX_VC_MAGIC_ALIVE);
  ink_debug_assert(connect_state == MUX_CONNECTED_IDLE);
  ink_debug_assert(num_clients == 0);

  Debug("muxvc", "state_idle: event %d", event);
  reentrancy_count++;

  switch (event) {
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE:
    {
      ink_debug_assert(process_event == data);
      process_event = NULL;
      connect_state = MUX_CONNECTED_TEARDOWN;

      int avail = write_vio->get_reader()->read_avail();
      if (avail == 0) {
        // All data sent. We're done.
        terminate_vc = true;
      } else {
        // We need to flush data
        SET_HANDLER(state_teardown);
        write_vio->nbytes = write_vio->ndone + avail;

        // check for rollover in bytes
        if (write_vio->nbytes < 0 || write_vio->nbytes == INT64_MAX) {
          write_vio = net_vc->do_io_write(this, avail, write_vio->get_reader());
        } else {
          write_vio->reenable();
        }

        // We don't want to hear from the read side anymore
        //
        //  FIX ME - Is there a race between new sessions
        //     being opened from the remote and the
        //     shutdown being issues?
        net_vc->do_io_shutdown(IO_SHUTDOWN_READ);
        read_vio = NULL;

        // FIX ME - should set inactivity timeout here to
        //   prevnt stuff from hanging around forever
      }
      break;
    }
  default:
    // Forward to the standard mux handle since
    //   this event should be coming from
    //   the underlying netvc
    r = state_handle_mux(event, data);
    break;
  }
  reentrancy_count--;
  if (terminate_vc && reentrancy_count == 0) {
    kill();
  }

  return r;
}



int
MuxVC::state_handle_mux_down(int event, void *data)
{

  ink_release_assert(magic == MUX_VC_MAGIC_ALIVE);

  Debug("muxvc", "state_handle_mux_down: event %d", event);
  reentrancy_count++;

  switch (event) {
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE:
    ink_debug_assert(process_event == data);
    process_event = NULL;
    cleanup_on_error();
    break;
  default:
    ink_release_assert(0);
    break;
  }
  reentrancy_count--;

  if (terminate_vc && reentrancy_count == 0) {
    kill();
  }

  return EVENT_CONT;
}

int
MuxVC::state_handle_mux(int event, void *data)
{

  ink_release_assert(magic == MUX_VC_MAGIC_ALIVE);

  Debug("muxvc", "state_handle_mux: event %d", event);
  reentrancy_count++;

  switch (event) {
  case VC_EVENT_WRITE_COMPLETE:
    // We hit INT64_MAX bytes.  Reset the I/O
    ink_debug_assert(data == write_vio);
    ink_debug_assert(write_vio->ndone == INT64_MAX);
    write_bytes_added -= write_vio->ndone;
    write_vio = net_vc->do_io_write(this, INT64_MAX, write_vio->buffer.reader());
    // FALL THROUGH
  case VC_EVENT_WRITE_READY:
    ink_debug_assert(data == write_vio);
    Debug("muxvc", "state_handle_mux: WRITE_READY, ndone: %d", write_vio->ndone);

    if (writes_blocked) {
      writes_blocked = false;
      process_clients();
    }
    break;
  case VC_EVENT_READ_COMPLETE:
    // We hit INT64_MAX bytes.  Reset the I/O
    ink_debug_assert(data == read_vio);
    read_vio = net_vc->do_io_read(this, INT64_MAX, read_buffer);
    // FALL THROUGH
  case VC_EVENT_READ_READY:
    ink_debug_assert(data == read_vio);
    /*Debug("muxvc", "state_handle_mux: READ_READY, ndone: %d, "
       "avail %d, high_water %d",
       read_vio->ndone,
       read_buffer_reader->read_avail(),
       (int) read_vio->buffer.writer()->high_water()); */
    process_read_data();
    read_vio->reenable();
    break;
  case EVENT_INTERVAL:
  case EVENT_IMMEDIATE:
    /*Debug("muxvc", "state_handle_mux: read_side: ndone: %d, "
       "avail %d, high_water %d",
       read_vio->ndone,
       read_buffer_reader->read_avail(),
       read_buffer->max_read_avail(),
       (int) read_buffer->high_water()); */
    ink_debug_assert(process_event == data);
    process_event = NULL;
    process_clients();
    read_vio->reenable();
    break;
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
    net_vc->do_io_close(0);
    net_vc = NULL;
    connect_state = MUX_CONNECTION_DROPPED;
    SET_HANDLER(MuxVC::state_handle_mux_down);
    cleanup_on_error();
    break;
  default:
    ink_release_assert(0);
  }

  reentrancy_count--;
  if (terminate_vc && reentrancy_count == 0) {
    kill();
  }

  return EVENT_CONT;
}

unsigned int
MuxVC::get_remote_ip()
{
  return (unsigned int) remote_addr.sin_addr.s_addr;
}

unsigned int
MuxVC::get_remote_port()
{
  return (unsigned int) remote_addr.sin_port;
}

MuxAcceptor::MuxAcceptor():
accept_action(NULL), call_cont(NULL), Continuation(new_ProxyMutex())
{
}

MuxAcceptor::~MuxAcceptor()
{
  if (accept_action) {
    accept_action->cancel();
    accept_action = NULL;
  }
}

void
MuxAcceptor::init(int port, Continuation * c)
{
  SET_HANDLER(MuxAcceptor::accept_handler);
  accept_action = netProcessor.accept(this, port);
  call_cont = c;
}

int
MuxAcceptor::accept_handler(int event, void *data)
{
  switch (event) {
  case NET_EVENT_ACCEPT:
    {
      MuxVC *new_vc = NEW(new MuxVC);
      Debug("muxvc", "Created new MuxVC @ 0x%x", new_vc);
      new_vc->init_from_accept((NetVConnection *) data, call_cont);
      break;
    }
  default:
    ink_release_assert(0);
    break;
  }
  return 0;
}

MuxProcessor muxProcessor;

MuxProcessor::MuxProcessor():
list_mutex(NULL), mux_list()
{
}

MuxProcessor::~MuxProcessor()
{
}

int
MuxProcessor::start()
{
  list_mutex = new_ProxyMutex();

  HttpAccept *http_accept = NEW(new HttpAccept(SERVER_PORT_DEFAULT));

  MuxAcceptor *new_accept = NEW(new MuxAcceptor);
  new_accept->init(9444, http_accept);

  mux_pages_init();

  return 0;
}

// void MuxProcessor::find_mux_internal(unsigned int ip, int port, MuxClient** rmux)
//
//     searches the existing mux list for a mux matching ip, port
//
//     if a matching mux is found,  calls back c with it
//        and returns MUX_FIND_FOUND
//
//     if no matching mux can be found returns
//           MUX_FIND_NOT_FOUND
//
//     if the search could not be compelted due to a lock
//       miss, returns MUX_FIND_RETRY
//
//
MuxFindResult_t MuxProcessor::find_mux_internal(Continuation * c, unsigned int ip, int port)
{


  EThread *
    my_ethread = this_ethread();
  MUTEX_TRY_LOCK(list_lock, list_mutex, my_ethread);

  if (!list_lock) {
    return MUX_FIND_RETRY;
  }
  MuxVC *
    current = mux_list.head;
  MuxVC *
    next = NULL;

  for (; current != NULL; current = next) {
    next = current->link.next;

    if (current->get_remote_ip() == ip && current->get_remote_port() == port) {

      MUTEX_TRY_LOCK(clock, current->mutex, my_ethread);
      if (!clock) {
        return MUX_FIND_RETRY;
      }

      if ((current->connect_state == MUX_CONNECTED_ACTIVE ||
           current->connect_state == MUX_CONNECTED_IDLE) &&
          (!is_action_tag_set("mux_limit") || current->num_clients <= 10)) {
        MuxClientVC *
          new_client = current->new_client();
        Debug("mux_open", "mux_find_internal cb with 0x%x", new_client);
        c->handleEvent(NET_EVENT_OPEN, new_client);
        return MUX_FIND_FOUND;
      }
    }
  }

  return MUX_FIND_NOT_FOUND;
}

Action *
MuxProcessor::get_mux_re(Continuation * c, unsigned int ip, int port)
{

  MuxGetCont *mgc = NULL;

  Debug("mux_open", "get_mux_re called for 0x%x", c);
  if (port == 0) {
    port = 9444;
  }

  MuxClientVC *new_mux = NULL;

  MuxFindResult_t r = find_mux_internal(c, ip, port);

  switch (r) {
  case MUX_FIND_FOUND:
    return ACTION_RESULT_DONE;
  case MUX_FIND_NOT_FOUND:
    mgc = NEW(new MuxGetCont);
    return mgc->init_for_new_mux(c, ip, port);
  case MUX_FIND_RETRY:
    // Lock miss while searching mux list so retry
    mgc = NEW(new MuxGetCont);
    return mgc->init_for_lock_miss(c, ip, port);
  default:
    ink_release_assert(0);
  }
}

MuxGetCont::MuxGetCont():
return_action(), mux_action(NULL), mux_vc(NULL), retry_event(NULL), ip(0), port(0)
{
}

MuxGetCont::~MuxGetCont()
{
  ink_debug_assert(mux_action == NULL);

  if (retry_event) {
    retry_event->cancel();
    retry_event = NULL;
  }

  *((Action *) & return_action) = NULL;
  mutex = NULL;
}

Action *
MuxGetCont::init_for_lock_miss(Continuation * c, unsigned int ip_arg, int port_arg)
{
  this->mutex = c->mutex;
  *((Action *) & return_action) = c;

  SET_HANDLER(MuxGetCont::lock_miss_handler);

  ip = ip_arg;
  port = port_arg;

  retry_event = eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));

  return &return_action;
}

Action *
MuxGetCont::init_for_new_mux(Continuation * c, unsigned int ip_arg, int port_arg)
{
  this->mutex = c->mutex;
  *((Action *) & return_action) = c;

  SET_HANDLER(MuxGetCont::new_mux_handler);

  mux_vc = NEW(new MuxVC);
  mux_vc->init();

  // Using take lock since it's a brand new mutex
  ProxyMutexPtr mref = mux_vc->mutex;
  MUTEX_TAKE_LOCK(mref, c->mutex->thread_holding);
  Action *tmp = mux_vc->do_connect(this, ip_arg, port_arg);
  Mutex_unlock(mref, mref->thread_holding);

  if (tmp != ACTION_RESULT_DONE) {
    mux_action = tmp;
    return &return_action;
  } else {
    return ACTION_RESULT_DONE;
  }
}

int
MuxGetCont::lock_miss_handler(int event, void *data)
{

  Event *call_event = (Event *) data;

  ink_release_assert(event == EVENT_INTERVAL);
  ink_debug_assert(retry_event == call_event);
  ink_debug_assert(this->mutex.m_ptr == return_action.mutex.m_ptr);

  retry_event = NULL;

  // Note, we've already got the continuation's mutex
  //   since we set our mutex to it's mutex
  if (return_action.cancelled) {
    ink_debug_assert(mux_action == NULL);
    retry_event = NULL;
    delete this;
    return EVENT_DONE;
  }

  MuxFindResult_t r = muxProcessor.find_mux_internal(return_action.continuation,
                                                     ip, port);

  switch (r) {
  case MUX_FIND_FOUND:
    break;
  case MUX_FIND_NOT_FOUND:{
      // Could not find a mux so create a new one
      SET_HANDLER(MuxGetCont::new_mux_handler);
      mux_vc = NEW(new MuxVC);
      mux_vc->init();

      Action *tmp = mux_vc->do_connect(this, ip, port);
      if (tmp != ACTION_RESULT_DONE) {
        mux_action = tmp;
      }
      break;
    }
  case MUX_FIND_RETRY:
    // Lock miss while searching mux list so retry
    retry_event = call_event;
    call_event->schedule_in(HRTIME_MSECONDS(10));
    return EVENT_DONE;
  default:
    ink_release_assert(0);
  }

  return EVENT_DONE;
}


int
MuxGetCont::new_mux_handler(int event, void *data)
{

  mux_action = NULL;

  switch (event) {
  case MUX_EVENT_OPEN:
    {
      ink_debug_assert(mux_vc->connect_state == MUX_CONNECTED_ACTIVE);
      Debug("mux_open", "[MuxGetCont::main_handler sending] adding to mux list");


      // Fix - use try_lock & retry
      MUTEX_TAKE_LOCK(muxProcessor.list_mutex, this_ethread());
      muxProcessor.mux_list.push(mux_vc);
      mux_vc->on_mux_list = true;
      Mutex_unlock(muxProcessor.list_mutex, muxProcessor.list_mutex->thread_holding);

      if (!return_action.cancelled) {
        MuxClientVC *new_client = mux_vc->new_client();
        Debug("mux_open", "[MuxGetCont::main_handler sending] callbak with NET_EVENT_OPEN");
        return_action.continuation->handleEvent(NET_EVENT_OPEN, new_client);
      }
      break;
    }
  case MUX_EVENT_OPEN_FAILED:
    {
      Debug("mux_open", "[MuxGetCont::main_handler sending] callbak with NET_EVENT_OPEN_FAILED");
      return_action.continuation->handleEvent(NET_EVENT_OPEN_FAILED, NULL);
      break;
    }
  default:
    ink_release_assert(0);
  }

  delete this;
  return 0;
}

/*************************************************************
 *
 *   STAT PAGES STUFF
 *
 **************************************************************/

class MuxPagesHandler:public BaseStatPagesHandler
{
public:
  MuxPagesHandler(Continuation * cont, HTTPHdr * header);
  ~MuxPagesHandler();

  int handle_muxvc_list(int event, void *edata);
  int handle_callback(int event, void *edata);
  int handle_mux_details(int event, void *data);

  Action action;
  char *request;
private:
    Arena arena;
  int32_t extract_id(const char *query);
  void dump_mux(MuxVC * mvc);
  void dump_mux_client(MuxClientVC * client);
};

MuxPagesHandler::MuxPagesHandler(Continuation * cont, HTTPHdr * header)
:BaseStatPagesHandler(new_ProxyMutex()), request(NULL)
{
  action = cont;

  URL *url;
  int length;

  url = header->url_get();
  request = (char *) url->path_get(&length);
  request = arena.str_store(request, length);

  if (strncmp(request, "mux_details", sizeof("mux_details")) == 0) {
    arena.str_free(request);
    request = (char *) url->query_get(&length);
    request = arena.str_store(request, length);
    SET_HANDLER(&MuxPagesHandler::handle_mux_details);
  } else {
    SET_HANDLER(&MuxPagesHandler::handle_muxvc_list);
  }
}

MuxPagesHandler::~MuxPagesHandler()
{
}

void
MuxPagesHandler::dump_mux_client(MuxClientVC * client)
{

  resp_begin_row();

  resp_begin_column();
  resp_add("%d", client->id);
  resp_end_column();

  // Write VIO
  resp_begin_column();
  resp_add("%d, %d", client->write_state.vio.nbytes, client->write_state.vio.ndone);
  resp_end_column();

  resp_begin_column();
  resp_add("%d, %d", client->write_state.enabled, client->write_state.shutdown);
  resp_end_column();


  // Read VIO
  resp_begin_column();
  resp_add("%d, %d", client->read_state.vio.nbytes, client->read_state.vio.ndone);
  resp_end_column();

  resp_begin_column();
  resp_add("%d, %d", client->read_state.enabled, client->read_state.shutdown);
  resp_end_column();

  resp_begin_column();
  resp_add("%d", (client->read_byte_bank) ? client->byte_bank_reader->read_avail() : 0);
  resp_end_column();

  resp_begin_column();
  resp_add("%d", client->other_side_closed);
  resp_end_column();

  resp_end_row();
}

void
MuxPagesHandler::dump_mux(MuxVC * mux)
{

  char *foo;
  unsigned int ip = mux->get_remote_ip();
  unsigned char *ip_ptr = (unsigned char *) &ip;

  UnixNetVConnection *unet_vc = (UnixNetVConnection *) mux->net_vc;

  resp_begin("Mux details");

  resp_add("<h3> Details for MuxVC Id %d </h3>\n", mux->id);

  resp_begin_item();
  resp_add("Connected to: %u.%u.%u.%u:%d", ip_ptr[0], ip_ptr[1], ip_ptr[2], ip_ptr[3], mux->get_remote_port());
  resp_end_item();

  if (mux->process_event) {
    resp_begin_item();
    resp_add("Process Event: 0x%X", mux->process_event);
    resp_end_item();
  }

  resp_begin_item();
  resp_add("Number of active clients: %d", mux->num_clients);
  resp_end_item();

  if (mux->read_vio) {
    resp_begin_item();
    resp_add("Read VIO: nbytes: %d, ndone %d, bytes avail %d",
             mux->read_vio->nbytes, mux->read_vio->ndone,
             (mux->read_buffer_reader != NULL) ? mux->read_buffer_reader->read_avail() : 0);
    resp_end_item();

    resp_begin_item();
    resp_add("Read Net State: enabled %d", unet_vc ? unet_vc->read.enabled : -1);
    resp_end_item();
  }

  if (mux->write_vio) {
    resp_begin_item();
    resp_add("Write VIO: nbytes: %d, ndone %d, in buffer bytes %d  blocks %d ",
             mux->write_vio->nbytes, mux->write_vio->ndone,
             (mux->write_vio->buffer.entry != NULL) ?
             mux->write_vio->buffer.entry->read_avail() : 0,
             (mux->write_vio->buffer.entry != NULL) ? mux->write_vio->buffer.entry->block_count() : 0);
    resp_end_item();

    resp_begin_item();
    resp_add("Write Net State: enabled %d", unet_vc ? unet_vc->write.enabled : -1);
    resp_end_item();
  }

  resp_add("<hr>\n");
  resp_add("<p> <h4> Clients: </h4> </p>");

  resp_begin_table(1, 3, 100);

  resp_begin_row();
  resp_begin_column();
  resp_add("Id");
  resp_end_column();
  resp_begin_column();
  resp_add("Write Nybytes, NDone");
  resp_end_column();
  resp_begin_column();
  resp_add("Write E  S");
  resp_end_column();
  resp_begin_column();
  resp_add("Read Nbytes, NDone");
  resp_end_column();
  resp_begin_column();
  resp_add("Read E  S");
  resp_end_column();
  resp_begin_column();
  resp_add("Byte Bank Size");
  resp_end_column();
  resp_begin_column();
  resp_add("Other Close");
  resp_end_column();
  resp_end_row();

  MuxClientVC *client = mux->active_clients.head;
  MuxClientVC *next = NULL;

  for (; client != NULL; client = next) {
    next = client->link.next;
    dump_mux_client(client);
  }

  resp_end_table();


  resp_end();
}

int
MuxPagesHandler::handle_mux_details(int event, void *data)
{

  ink_debug_assert(event == EVENT_IMMEDIATE || event == EVENT_INTERVAL);
  Event *call_event = (Event *) data;

  int32_t mux_id = extract_id(request);

  if (mux_id < 0) {
    resp_begin("Mux Pages Error");
    resp_add("<b>Unable to extract id</b>\n");
    resp_end();
    return handle_callback(EVENT_NONE, NULL);
  }

  MUTEX_TRY_LOCK(pLock, muxProcessor.list_mutex, call_event->ethread);

  if (!pLock) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    return EVENT_DONE;
  }

  MuxVC *mux = muxProcessor.mux_list.head;
  MuxVC *next = NULL;

  for (; mux != NULL; mux = next) {
    next = mux->link.next;

    if (mux->id == mux_id) {
      break;
    }
  }

  if (mux == NULL) {
    resp_begin("Mux Pages Error");
    resp_add("<b>Unable to find id %d</b>\n", mux_id);
    resp_end();
    return handle_callback(EVENT_NONE, NULL);
  }

  MUTEX_TRY_LOCK(mLock, mux->mutex, call_event->ethread);
  if (!mLock) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    return EVENT_DONE;
  }

  dump_mux(mux);
  handle_callback(EVENT_NONE, NULL);

  return EVENT_DONE;
}

int
MuxPagesHandler::handle_muxvc_list(int event, void *data)
{

  ink_debug_assert(event == EVENT_IMMEDIATE || event == EVENT_INTERVAL);
  Event *call_event = (Event *) data;

  MUTEX_TRY_LOCK(lock, muxProcessor.list_mutex, call_event->ethread);

  if (!lock) {
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    return EVENT_DONE;
  }

  resp_begin("MuxVC List");

  MuxVC *mux = muxProcessor.mux_list.head;
  MuxVC *next = NULL;

  for (; mux != NULL; mux = next) {
    next = mux->link.next;

    unsigned int ip = mux->get_remote_ip();
    int port = mux->get_remote_port();
    unsigned char *ip_ptr = (unsigned char *) &ip;

    resp_begin_item();
    resp_add("id: <a href=\"http://{mux}/mux_details?id=%d\"> "
             "%d </a> | %u.%u.%u.%u:%d | %d clients\n",
             mux->id, mux->id, ip_ptr[0], ip_ptr[1], ip_ptr[2], ip_ptr[3], port, mux->num_clients);
    resp_end_item();
  }

  resp_end();
  handle_callback(EVENT_NONE, NULL);

  return EVENT_DONE;
}

int
MuxPagesHandler::handle_callback(int event, void *edata)
{
  MUTEX_TRY_LOCK(trylock, action.mutex, this_ethread());
  if (!trylock) {
    SET_HANDLER(&MuxPagesHandler::handle_callback);
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(10));
    return EVENT_DONE;
  }

  if (!action.cancelled) {
    if (response) {
      StatPageData data;

      data.data = response;
      data.type = ats_strdup("text/html");
      data.length = response_length;
      response = NULL;

      action.continuation->handleEvent(STAT_PAGE_SUCCESS, &data);
    } else {
      action.continuation->handleEvent(STAT_PAGE_FAILURE, NULL);
    }
  }

  delete this;
  return EVENT_DONE;
}

int32_t
MuxPagesHandler::extract_id(const char *query)
{
  char *p;
  int32_t id;

  p = (char *) strstr(query, "id=");
  if (!p) {
    return -1;
  }
  p += sizeof("id=") - 1;

  id = ink_atoi(p);

  // Check to see if we found the id
  if (id == 0) {
    if (*p == '0' && *(p + 1) == '\0') {
      return 0;
    } else {
      return -1;
    }
  } else {
    return id;
  }
}

static Action *
mux_pages_callback(Continuation * cont, HTTPHdr * header)
{
  MuxPagesHandler *handler;

  handler = NEW(new MuxPagesHandler(cont, header));
  eventProcessor.schedule_imm(handler, ET_CALL);

  return &handler->action;
}

void
mux_pages_init()
{
  statPagesManager.register_http("mux", mux_pages_callback);
}

/*************************************************************
 *
 *   REGRESSION TEST STUFF
 *
 **************************************************************/

class MUXTestDriver:public NetTestDriver
{
public:
  MUXTestDriver();
  ~MUXTestDriver();

  void start_tests(RegressionTest * r_arg, int *pstatus_arg);
  int main_handler(int event, void *data);

private:
    MuxAcceptor * regress_accept;
  Action *pending_action;

  int i;
  int completions_received;
  RegressionTest *r;
  int *pstatus;

  void start_next_test();
  void start_active_side(NetVConnection * p_vc);
  void start_passive_side(NetVConnection * p_vc);
};

MUXTestDriver::MUXTestDriver():
regress_accept(NULL), pending_action(NULL), i(0), completions_received(0), r(NULL), pstatus(NULL), NetTestDriver()
{
}

MUXTestDriver::~MUXTestDriver()
{
  mutex = NULL;

  if (regress_accept) {
    delete regress_accept;
    regress_accept = NULL;
  }

  if (pending_action) {
    pending_action->cancel();
    pending_action = NULL;
  }
}


void
MUXTestDriver::start_tests(RegressionTest * r_arg, int *pstatus_arg)
{
  mutex = new_ProxyMutex();
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());

  r = r_arg;
  pstatus = pstatus_arg;

  SET_HANDLER(&MUXTestDriver::main_handler);

  regress_accept = NEW(new MuxAcceptor);
  regress_accept->init(9555, this);

  start_next_test();
}


void
MUXTestDriver::start_next_test()
{

  int next_index = i * 2;
  if (next_index >= num_netvc_tests) {
    // We are done - // FIX - PASS or FAIL?
    if (errors == 0) {
      *pstatus = REGRESSION_TEST_PASSED;
    } else {
      *pstatus = REGRESSION_TEST_FAILED;
    }
    return;
  }

  Debug("mux_test", "Starting test %s", netvc_tests_def[next_index].test_name);
  completions_received = 0;

  ink_debug_assert(pending_action == NULL);
  Action *tmp = muxProcessor.get_mux_re(this, inet_addr("127.0.0.1"), 9555);

  if (tmp != ACTION_RESULT_DONE) {
    pending_action = tmp;
  }
}

void
MUXTestDriver::start_active_side(NetVConnection * a_vc)
{
  int a_index = i * 2;

  NetVCTest *a = NEW(new NetVCTest);
  a->init_test(NET_VC_TEST_ACTIVE, this, a_vc, r, &netvc_tests_def[a_index], "MuxVC", "mux_test_detail");
  a->start_test();
}

void
MUXTestDriver::start_passive_side(NetVConnection * p_vc)
{
  int p_index = (i * 2) + 1;

  NetVCTest *p = NEW(new NetVCTest);
  p->init_test(NET_VC_TEST_PASSIVE, this, p_vc, r, &netvc_tests_def[p_index], "MuxVC", "mux_test_detail");
  p->start_test();
}


int
MUXTestDriver::main_handler(int event, void *data)
{

  Debug("mux_test_detail", "MUXTestDriver::main_handler recevied event %d", event);

  switch (event) {
  case NET_EVENT_OPEN:
    {
      // We opened test mux vc so start testing
      pending_action = NULL;
      start_active_side((NetVConnection *) data);
      break;
    }
  case NET_EVENT_OPEN_FAILED:
    {
      // Open of the test vc failed so give up
      pending_action = NULL;
      Warning("mux regression failed - could not open localhost muxvc");
      *pstatus = REGRESSION_TEST_FAILED;
      delete this;
      break;
    }
  case NET_EVENT_ACCEPT:
    {
      // New test client
      start_passive_side((NetVConnection *) data);
      break;
    }
  case EVENT_IMMEDIATE:
    {
      // Signifies a completion of one side of the test
      completions_received++;

      if (completions_received == 2) {
        i++;
        start_next_test();
      }
      break;
    }
  }

  return 0;
}

REGRESSION_TEST(MUX) (RegressionTest * t, int atype, int *pstatus) {
  MUXTestDriver *driver = NEW(new MUXTestDriver);
  driver->start_tests(t, pstatus);
}
