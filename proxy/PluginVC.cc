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

   PluginVC.cc

   Description: Allows bi-directional transfer for data from one
      continuation to another via a mechanism that impersonates a
      NetVC.  Should implement all external attributes of NetVConnections.

   Since data is transfered within Traffic Server, this is a two
   headed beast.  One NetVC on initiating side (active side) and
   one NetVC on the receiving side (passive side).

   The two NetVC subclasses, PluginVC, are part PluginVCCore object.  All
   three objects share the same mutex.  That mutex is required
   for doing operations that affect the shared buffers,
   read state from the PluginVC on the other side or deal with deallocation.

   To simplify the code, all data passing through the system goes initially
   into a shared buffer.  There are two shared buffers, one for each
   direction of the connection.  While it's more efficient to transfer
   the data from one buffer to another directly, this creates a lot
   of tricky conditions since you must be holding the lock for both
   sides, in additional this VC's lock.  Additionally, issues like
   watermarks are very hard to deal with.  Since we try to
   to move data by IOBufferData references the efficiency penalty shouldn't
   be too bad and if it is a big penalty, a brave soul can reimplement
   to move the data directly without the intermediate buffer.

   Locking is difficult issue for this multi-headed beast.  In each
   PluginVC, there a two locks. The one we got from our PluginVCCore and
   the lock from the state machine using the PluginVC.  The read side
   lock & the write side lock must be the same.  The regular net processor has
   this constraint as well.  In order to handle scheduling of retry events cleanly,
   we have two event pointers, one for each lock.  sm_lock_retry_event can only
   be changed while holding the using state machine's lock and
   core_lock_retry_event can only be manipulated while holding the PluginVC's
   lock.  On entry to PluginVC::main_handler, we obtain all the locks
   before looking at the events.  If we can't get all the locks
   we reschedule the event for further retries.  Since all the locks are
   obtained in the beginning of the handler, we know we are running
   exclusively in the later parts of the handler and we will
   be free from do_io or reenable calls on the PluginVC.

   The assumption is made (consistent with IO Core spec) that any close,
   shutdown, reenable, or do_io_{read,write) operation is done by the callee
   while holding the lock for that side of the operation.


 ****************************************************************************/

#include "PluginVC.h"
#include "P_EventSystem.h"
#include "P_Net.h"
#include "tscore/Regression.h"

#define PVC_LOCK_RETRY_TIME HRTIME_MSECONDS(10)
#define PVC_DEFAULT_MAX_BYTES 32768
#define MIN_BLOCK_TRANSFER_BYTES 128

#define PVC_TYPE ((vc_type == PLUGIN_VC_ACTIVE) ? "Active" : "Passive")

PluginVC::PluginVC(PluginVCCore *core_obj)
  : NetVConnection(),
    magic(PLUGIN_VC_MAGIC_ALIVE),
    vc_type(PLUGIN_VC_UNKNOWN),
    core_obj(core_obj),
    other_side(nullptr),
    read_state(),
    write_state(),
    need_read_process(false),
    need_write_process(false),
    closed(false),
    sm_lock_retry_event(nullptr),
    core_lock_retry_event(nullptr),
    deletable(false),
    reentrancy_count(0),
    active_timeout(0),
    active_event(nullptr),
    inactive_timeout(0),
    inactive_timeout_at(0),
    inactive_event(nullptr),
    plugin_tag(nullptr),
    plugin_id(0)
{
  ink_assert(core_obj != nullptr);
  SET_HANDLER(&PluginVC::main_handler);
}

PluginVC::~PluginVC()
{
  mutex = nullptr;
}

int
PluginVC::main_handler(int event, void *data)
{
  Debug("pvc_event", "[%u] %s: Received event %d", core_obj->id, PVC_TYPE, event);

  ink_release_assert(event == EVENT_INTERVAL || event == EVENT_IMMEDIATE);
  ink_release_assert(magic == PLUGIN_VC_MAGIC_ALIVE);
  ink_assert(!deletable);
  ink_assert(data != nullptr);

  Event *call_event   = (Event *)data;
  EThread *my_ethread = mutex->thread_holding;
  ink_release_assert(my_ethread != nullptr);

  bool read_mutex_held             = false;
  bool write_mutex_held            = false;
  Ptr<ProxyMutex> read_side_mutex  = read_state.vio.mutex;
  Ptr<ProxyMutex> write_side_mutex = write_state.vio.mutex;

  if (read_side_mutex) {
    read_mutex_held = MUTEX_TAKE_TRY_LOCK(read_side_mutex, my_ethread);

    if (!read_mutex_held) {
      if (call_event != inactive_event) {
        call_event->schedule_in(PVC_LOCK_RETRY_TIME);
      }
      return 0;
    }

    if (read_side_mutex != read_state.vio.mutex) {
      // It's possible some swapped the mutex on us before
      //  we were able to grab it
      Mutex_unlock(read_side_mutex, my_ethread);
      if (call_event != inactive_event) {
        call_event->schedule_in(PVC_LOCK_RETRY_TIME);
      }
      return 0;
    }
  }

  if (write_side_mutex) {
    write_mutex_held = MUTEX_TAKE_TRY_LOCK(write_side_mutex, my_ethread);

    if (!write_mutex_held) {
      if (read_mutex_held) {
        Mutex_unlock(read_side_mutex, my_ethread);
      }
      if (call_event != inactive_event) {
        call_event->schedule_in(PVC_LOCK_RETRY_TIME);
      }
      return 0;
    }

    if (write_side_mutex != write_state.vio.mutex) {
      // It's possible some swapped the mutex on us before
      //  we were able to grab it
      Mutex_unlock(write_side_mutex, my_ethread);
      if (read_mutex_held) {
        Mutex_unlock(read_side_mutex, my_ethread);
      }
      if (call_event != inactive_event) {
        call_event->schedule_in(PVC_LOCK_RETRY_TIME);
      }
      return 0;
    }
  }
  // We've got all the locks so there should not be any
  //   other calls active
  ink_release_assert(reentrancy_count == 0);

  if (closed) {
    process_close();

    if (read_mutex_held) {
      Mutex_unlock(read_side_mutex, my_ethread);
    }

    if (write_mutex_held) {
      Mutex_unlock(write_side_mutex, my_ethread);
    }

    return 0;
  }
  // We can get closed while we're calling back the
  //  continuation.  Set the reentrancy count so we know
  //  we could be calling the continuation and that we
  //  need to defer close processing
  reentrancy_count++;

  if (call_event == active_event) {
    process_timeout(&active_event, VC_EVENT_ACTIVE_TIMEOUT);
  } else if (call_event == inactive_event) {
    if (inactive_timeout_at && inactive_timeout_at < Thread::get_hrtime()) {
      process_timeout(&inactive_event, VC_EVENT_INACTIVITY_TIMEOUT);
    }
  } else {
    if (call_event == sm_lock_retry_event) {
      sm_lock_retry_event = nullptr;
    } else {
      ink_release_assert(call_event == core_lock_retry_event);
      core_lock_retry_event = nullptr;
    }

    if (need_read_process) {
      process_read_side(false);
    }

    if (need_write_process && !closed) {
      process_write_side(false);
    }
  }

  reentrancy_count--;
  if (closed) {
    process_close();
  }

  if (read_mutex_held) {
    Mutex_unlock(read_side_mutex, my_ethread);
  }

  if (write_mutex_held) {
    Mutex_unlock(write_side_mutex, my_ethread);
  }

  return 0;
}

VIO *
PluginVC::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  ink_assert(!closed);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  if (buf) {
    read_state.vio.buffer.writer_for(buf);
  } else {
    read_state.vio.buffer.clear();
  }

  // Note: we set vio.op last because process_read_side looks at it to
  //  tell if the VConnection is active.
  read_state.vio.mutex     = c ? c->mutex : this->mutex;
  read_state.vio.cont      = c;
  read_state.vio.nbytes    = nbytes;
  read_state.vio.ndone     = 0;
  read_state.vio.vc_server = (VConnection *)this;
  read_state.vio.op        = VIO::READ;

  Debug("pvc", "[%u] %s: do_io_read for %" PRId64 " bytes", core_obj->id, PVC_TYPE, nbytes);

  // Since reentrant callbacks are not allowed on from do_io
  //   functions schedule ourselves get on a different stack
  need_read_process = true;
  setup_event_cb(0, &sm_lock_retry_event);

  return &read_state.vio;
}

VIO *
PluginVC::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuffer, bool owner)
{
  ink_assert(!closed);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  if (abuffer) {
    ink_assert(!owner);
    write_state.vio.buffer.reader_for(abuffer);
  } else {
    write_state.vio.buffer.clear();
  }

  // Note: we set vio.op last because process_write_side looks at it to
  //  tell if the VConnection is active.
  write_state.vio.mutex     = c ? c->mutex : this->mutex;
  write_state.vio.cont      = c;
  write_state.vio.nbytes    = nbytes;
  write_state.vio.ndone     = 0;
  write_state.vio.vc_server = (VConnection *)this;
  write_state.vio.op        = VIO::WRITE;

  Debug("pvc", "[%u] %s: do_io_write for %" PRId64 " bytes", core_obj->id, PVC_TYPE, nbytes);

  // Since reentrant callbacks are not allowed on from do_io
  //   functions schedule ourselves get on a different stack
  need_write_process = true;
  setup_event_cb(0, &sm_lock_retry_event);

  return &write_state.vio;
}

void
PluginVC::reenable(VIO *vio)
{
  ink_assert(!closed);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);
  ink_assert(vio->mutex->thread_holding == this_ethread());

  Ptr<ProxyMutex> sm_mutex = vio->mutex;
  SCOPED_MUTEX_LOCK(lock, sm_mutex, this_ethread());

  Debug("pvc", "[%u] %s: reenable %s", core_obj->id, PVC_TYPE, (vio->op == VIO::WRITE) ? "Write" : "Read");

  if (vio->op == VIO::WRITE) {
    ink_assert(vio == &write_state.vio);
    need_write_process = true;
  } else if (vio->op == VIO::READ) {
    need_read_process = true;
  } else {
    ink_release_assert(0);
  }
  setup_event_cb(0, &sm_lock_retry_event);
}

void
PluginVC::reenable_re(VIO *vio)
{
  ink_assert(!closed);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);
  ink_assert(vio->mutex->thread_holding == this_ethread());

  Debug("pvc", "[%u] %s: reenable_re %s", core_obj->id, PVC_TYPE, (vio->op == VIO::WRITE) ? "Write" : "Read");

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  ++reentrancy_count;

  if (vio->op == VIO::WRITE) {
    ink_assert(vio == &write_state.vio);
    need_write_process = true;
    process_write_side(false);
  } else if (vio->op == VIO::READ) {
    ink_assert(vio == &read_state.vio);
    need_read_process = true;
    process_read_side(false);
  } else {
    ink_release_assert(0);
  }

  --reentrancy_count;

  // To process the close, we need the lock
  //   for the PluginVC.  Schedule an event
  //   to make sure we get it
  if (closed) {
    setup_event_cb(0, &sm_lock_retry_event);
  }
}

void
PluginVC::do_io_close(int /* flag ATS_UNUSED */)
{
  ink_assert(!closed);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  Debug("pvc", "[%u] %s: do_io_close", core_obj->id, PVC_TYPE);

  SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
  if (!closed) { // if already closed, need to do nothing.
    closed = true;

    // If re-entered then that earlier handler will clean up, otherwise set up a ping
    // to drive that process (too dangerous to do it here).
    if (reentrancy_count <= 0) {
      setup_event_cb(0, &sm_lock_retry_event);
    }
  }
}

void
PluginVC::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(!closed);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  switch (howto) {
  case IO_SHUTDOWN_READ:
    read_state.shutdown = true;
    break;
  case IO_SHUTDOWN_WRITE:
    write_state.shutdown = true;
    break;
  case IO_SHUTDOWN_READWRITE:
    read_state.shutdown  = true;
    write_state.shutdown = true;
    break;
  }
}

// int PluginVC::transfer_bytes(MIOBuffer* transfer_to,
//                              IOBufferReader* transfer_from, int act_on)
//
//   Takes care of transfering bytes from a reader to another buffer
//      In the case of large transfers, we move blocks.  In the case
//      of small transfers we copy data so as to not build too many
//      buffer blocks
//
// Args:
//   transfer_to:  buffer to copy to
//   transfer_from:  buffer_copy_from
//   act_on: is the max number of bytes we are to copy.  There must
//          be at least act_on bytes available from transfer_from
//
// Returns number of bytes transfered
//
int64_t
PluginVC::transfer_bytes(MIOBuffer *transfer_to, IOBufferReader *transfer_from, int64_t act_on)
{
  int64_t total_added = 0;

  ink_assert(act_on <= transfer_from->read_avail());

  while (act_on > 0) {
    int64_t block_read_avail = transfer_from->block_read_avail();
    int64_t to_move          = std::min(act_on, block_read_avail);
    int64_t moved            = 0;

    if (to_move <= 0) {
      break;
    }

    if (to_move >= MIN_BLOCK_TRANSFER_BYTES) {
      moved = transfer_to->write(transfer_from, to_move, 0);
    } else {
      // We have a really small amount of data.  To make
      //  sure we don't get a huge build up of blocks which
      //  can lead to stack overflows if the buffer is destroyed
      //  before we read from it, we need copy over to the new
      //  buffer instead of doing a block transfer
      moved = transfer_to->write(transfer_from->start(), to_move);

      if (moved == 0) {
        // We are out of buffer space
        break;
      }
    }

    act_on -= moved;
    transfer_from->consume(moved);
    total_added += moved;
  }

  return total_added;
}

// void PluginVC::process_write_side(bool cb_ok)
//
//   This function may only be called while holding
//      this->mutex & while it is ok to callback the
//      write side continuation
//
//   Does write side processing
//
void
PluginVC::process_write_side(bool other_side_call)
{
  ink_assert(!deletable);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  MIOBuffer *core_buffer = (vc_type == PLUGIN_VC_ACTIVE) ? core_obj->a_to_p_buffer : core_obj->p_to_a_buffer;

  Debug("pvc", "[%u] %s: process_write_side", core_obj->id, PVC_TYPE);
  need_write_process = false;

  // Check write_state
  if (write_state.vio.op != VIO::WRITE || closed || write_state.shutdown) {
    return;
  }

  // Check the state of our write buffer as well as ntodo
  int64_t ntodo = write_state.vio.ntodo();
  if (ntodo == 0) {
    return;
  }

  IOBufferReader *reader = write_state.vio.get_reader();
  int64_t bytes_avail    = reader->read_avail();
  int64_t act_on         = std::min(bytes_avail, ntodo);

  Debug("pvc", "[%u] %s: process_write_side; act_on %" PRId64 "", core_obj->id, PVC_TYPE, act_on);

  if (other_side->closed || other_side->read_state.shutdown) {
    write_state.vio.cont->handleEvent(VC_EVENT_ERROR, &write_state.vio);
    return;
  }

  if (act_on <= 0) {
    if (ntodo > 0) {
      // Notify the continuation that we are "disabling"
      //  ourselves due to to nothing to write
      write_state.vio.cont->handleEvent(VC_EVENT_WRITE_READY, &write_state.vio);
    }
    return;
  }
  // Bytes available, try to transfer to the PluginVCCore
  //   intermediate buffer
  //
  int64_t buf_space = PVC_DEFAULT_MAX_BYTES - core_buffer->max_read_avail();
  if (buf_space <= 0) {
    Debug("pvc", "[%u] %s: process_write_side no buffer space", core_obj->id, PVC_TYPE);
    return;
  }
  act_on = std::min(act_on, buf_space);

  int64_t added = transfer_bytes(core_buffer, reader, act_on);
  if (added < 0) {
    // Couldn't actually get the buffer space.  This only
    //   happens on small transfers with the above
    //   PVC_DEFAULT_MAX_BYTES factor doesn't apply
    Debug("pvc", "[%u] %s: process_write_side out of buffer space", core_obj->id, PVC_TYPE);
    return;
  }

  write_state.vio.ndone += added;

  Debug("pvc", "[%u] %s: process_write_side; added %" PRId64 "", core_obj->id, PVC_TYPE, added);

  if (write_state.vio.ntodo() == 0) {
    write_state.vio.cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &write_state.vio);
  } else {
    write_state.vio.cont->handleEvent(VC_EVENT_WRITE_READY, &write_state.vio);
  }

  update_inactive_time();

  // Wake up the read side on the other side to process these bytes
  if (!other_side->closed) {
    if (!other_side_call) {
      /* To clear the `need_read_process`, the mutexes must be obtained:
       *
       *   - PluginVC::mutex
       *   - PluginVC::read_state.vio.mutex
       *
       */
      if (other_side->read_state.vio.op != VIO::READ || other_side->closed || other_side->read_state.shutdown) {
        // Just return, no touch on `other_side->need_read_process`.
        return;
      }
      // Acquire the lock of the read side continuation
      EThread *my_ethread = mutex->thread_holding;
      ink_assert(my_ethread != nullptr);
      MUTEX_TRY_LOCK(lock, other_side->read_state.vio.mutex, my_ethread);
      if (!lock.is_locked()) {
        Debug("pvc_event", "[%u] %s: process_read_side from other side lock miss, retrying", other_side->core_obj->id,
              ((other_side->vc_type == PLUGIN_VC_ACTIVE) ? "Active" : "Passive"));

        // set need_read_process to enforce the read processing
        other_side->need_read_process = true;
        other_side->setup_event_cb(PVC_LOCK_RETRY_TIME, &other_side->core_lock_retry_event);
        return;
      }

      other_side->process_read_side(true);
    } else {
      other_side->read_state.vio.reenable();
    }
  }
}

// void PluginVC::process_read_side()
//
//   This function may only be called while holding
//      this->mutex & while it is ok to callback the
//      read side continuation
//
//   Does read side processing
//
void
PluginVC::process_read_side(bool other_side_call)
{
  ink_assert(!deletable);
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  // TODO: Never used??
  // MIOBuffer *core_buffer;

  IOBufferReader *core_reader;

  if (vc_type == PLUGIN_VC_ACTIVE) {
    // core_buffer = core_obj->p_to_a_buffer;
    core_reader = core_obj->p_to_a_reader;
  } else {
    ink_assert(vc_type == PLUGIN_VC_PASSIVE);
    // core_buffer = core_obj->a_to_p_buffer;
    core_reader = core_obj->a_to_p_reader;
  }

  Debug("pvc", "[%u] %s: process_read_side", core_obj->id, PVC_TYPE);
  need_read_process = false;

  // Check read_state
  if (read_state.vio.op != VIO::READ || closed || read_state.shutdown) {
    return;
  }

  // Check the state of our read buffer as well as ntodo
  int64_t ntodo = read_state.vio.ntodo();
  if (ntodo == 0) {
    return;
  }

  int64_t bytes_avail = core_reader->read_avail();
  int64_t act_on      = std::min(bytes_avail, ntodo);

  Debug("pvc", "[%u] %s: process_read_side; act_on %" PRId64 "", core_obj->id, PVC_TYPE, act_on);

  if (act_on <= 0) {
    if (other_side->closed || other_side->write_state.shutdown) {
      read_state.vio.cont->handleEvent(VC_EVENT_EOS, &read_state.vio);
    }
    return;
  }
  // Bytes available, try to transfer from the PluginVCCore
  //   intermediate buffer
  //
  MIOBuffer *output_buffer = read_state.vio.get_writer();

  int64_t water_mark = output_buffer->water_mark;
  water_mark         = std::max(water_mark, static_cast<int64_t>(PVC_DEFAULT_MAX_BYTES));
  int64_t buf_space  = water_mark - output_buffer->max_read_avail();
  if (buf_space <= 0) {
    Debug("pvc", "[%u] %s: process_read_side no buffer space", core_obj->id, PVC_TYPE);
    return;
  }
  act_on = std::min(act_on, buf_space);

  int64_t added = transfer_bytes(output_buffer, core_reader, act_on);
  if (added <= 0) {
    // Couldn't actually get the buffer space.  This only
    //   happens on small transfers with the above
    //   PVC_DEFAULT_MAX_BYTES factor doesn't apply
    Debug("pvc", "[%u] %s: process_read_side out of buffer space", core_obj->id, PVC_TYPE);
    return;
  }

  read_state.vio.ndone += added;

  Debug("pvc", "[%u] %s: process_read_side; added %" PRId64 "", core_obj->id, PVC_TYPE, added);

  if (read_state.vio.ntodo() == 0) {
    read_state.vio.cont->handleEvent(VC_EVENT_READ_COMPLETE, &read_state.vio);
  } else {
    read_state.vio.cont->handleEvent(VC_EVENT_READ_READY, &read_state.vio);
  }

  update_inactive_time();

  // Wake up the other side so it knows there is space available in
  //  intermediate buffer
  if (!other_side->closed) {
    if (!other_side_call) {
      /* To clear the `need_write_process`, the mutexes must be obtained:
       *
       *   - PluginVC::mutex
       *   - PluginVC::write_state.vio.mutex
       *
       */
      if (other_side->write_state.vio.op != VIO::WRITE || other_side->closed || other_side->write_state.shutdown) {
        // Just return, no touch on `other_side->need_write_process`.
        return;
      }
      // Acquire the lock of the write side continuation
      EThread *my_ethread = mutex->thread_holding;
      ink_assert(my_ethread != nullptr);
      MUTEX_TRY_LOCK(lock, other_side->write_state.vio.mutex, my_ethread);
      if (!lock.is_locked()) {
        Debug("pvc_event", "[%u] %s: process_write_side from other side lock miss, retrying", other_side->core_obj->id,
              ((other_side->vc_type == PLUGIN_VC_ACTIVE) ? "Active" : "Passive"));

        // set need_write_process to enforce the write processing
        other_side->need_write_process = true;
        other_side->setup_event_cb(PVC_LOCK_RETRY_TIME, &other_side->core_lock_retry_event);
        return;
      }

      other_side->process_write_side(true);
    } else {
      other_side->write_state.vio.reenable();
    }
  }
}

// void PluginVC::process_read_close()
//
//   This function may only be called while holding
//      this->mutex
//
//   Tries to close the and dealloc the the vc
//
void
PluginVC::process_close()
{
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  Debug("pvc", "[%u] %s: process_close", core_obj->id, PVC_TYPE);

  if (!deletable) {
    deletable = true;
  }

  if (sm_lock_retry_event) {
    sm_lock_retry_event->cancel();
    sm_lock_retry_event = nullptr;
  }

  if (core_lock_retry_event) {
    core_lock_retry_event->cancel();
    core_lock_retry_event = nullptr;
  }

  if (active_event) {
    active_event->cancel();
    active_event = nullptr;
  }

  if (inactive_event) {
    inactive_event->cancel();
    inactive_event      = nullptr;
    inactive_timeout_at = 0;
  }
  // If the other side of the PluginVC is not closed
  //  we need to force it process both living sides
  //  of the connection in order that it recognizes
  //  the close
  if (!other_side->closed && core_obj->connected) {
    other_side->need_write_process = true;
    other_side->need_read_process  = true;
    other_side->setup_event_cb(0, &other_side->core_lock_retry_event);
  }

  core_obj->attempt_delete();
}

// void PluginVC::process_timeout(Event** e, int event_to_send, Event** our_eptr)
//
//   Handles sending timeout event to the VConnection.  e is the event we got
//     which indicates the timeout.  event_to_send is the event to the
//     vc user.  e is a pointer to either inactive_event,
//     or active_event.  If we successfully send the timeout to vc user,
//     we clear the pointer, otherwise we reschedule it.
//
//   Because the possibility of reentrant close from vc user, we don't want to
//      touch any state after making the call back
//
void
PluginVC::process_timeout(Event **e, int event_to_send)
{
  ink_assert(*e == inactive_event || *e == active_event);

  if (closed) {
    // already closed, ignore the timeout event
    // to avoid handle_event asserting use-after-free
    clear_event(e);
    return;
  }

  if (read_state.vio.op == VIO::READ && !read_state.shutdown && read_state.vio.ntodo() > 0) {
    MUTEX_TRY_LOCK(lock, read_state.vio.mutex, (*e)->ethread);
    if (!lock.is_locked()) {
      if (*e == active_event) {
        // Only reschedule active_event due to inactive_event is perorid event.
        (*e)->schedule_in(PVC_LOCK_RETRY_TIME);
      }
      return;
    }
    clear_event(e);
    read_state.vio.cont->handleEvent(event_to_send, &read_state.vio);
  } else if (write_state.vio.op == VIO::WRITE && !write_state.shutdown && write_state.vio.ntodo() > 0) {
    MUTEX_TRY_LOCK(lock, write_state.vio.mutex, (*e)->ethread);
    if (!lock.is_locked()) {
      if (*e == active_event) {
        // Only reschedule active_event due to inactive_event is perorid event.
        (*e)->schedule_in(PVC_LOCK_RETRY_TIME);
      }
      return;
    }
    clear_event(e);
    write_state.vio.cont->handleEvent(event_to_send, &write_state.vio);
  } else {
    clear_event(e);
  }
}

void
PluginVC::clear_event(Event **e)
{
  if (e == nullptr || *e == nullptr)
    return;
  if (*e == inactive_event) {
    inactive_event->cancel();
    inactive_timeout_at = 0;
  }
  *e = nullptr;
}

void
PluginVC::update_inactive_time()
{
  if (inactive_event && inactive_timeout) {
    // inactive_event->cancel();
    // inactive_event = eventProcessor.schedule_in(this, inactive_timeout);
    inactive_timeout_at = Thread::get_hrtime() + inactive_timeout;
  }
}

// void PluginVC::setup_event_cb(ink_hrtime in)
//
//    Setup up the event processor to call us back.
//      We've got two different event pointers to handle
//      locking issues
//
void
PluginVC::setup_event_cb(ink_hrtime in, Event **e_ptr)
{
  ink_assert(magic == PLUGIN_VC_MAGIC_ALIVE);

  if (*e_ptr == nullptr) {
    // We locked the pointer so we can now allocate an event
    //   to call us back
    if (in == 0) {
      if (this_ethread()->tt == REGULAR) {
        *e_ptr = this_ethread()->schedule_imm_local(this);
      } else {
        *e_ptr = eventProcessor.schedule_imm(this);
      }
    } else {
      if (this_ethread()->tt == REGULAR) {
        *e_ptr = this_ethread()->schedule_in_local(this, in);
      } else {
        *e_ptr = eventProcessor.schedule_in(this, in);
      }
    }
  }
}

void
PluginVC::set_active_timeout(ink_hrtime timeout_in)
{
  active_timeout = timeout_in;

  // FIX - Do we need to handle the case where the timeout is set
  //   but no io has been done?
  if (active_event) {
    ink_assert(!active_event->cancelled);
    active_event->cancel();
    active_event = nullptr;
  }

  if (active_timeout > 0) {
    active_event = eventProcessor.schedule_in(this, active_timeout);
  }
}

void
PluginVC::set_inactivity_timeout(ink_hrtime timeout_in)
{
  inactive_timeout = timeout_in;
  if (inactive_timeout != 0) {
    inactive_timeout_at = Thread::get_hrtime() + inactive_timeout;
    if (inactive_event == nullptr) {
      inactive_event = eventProcessor.schedule_every(this, HRTIME_SECONDS(1));
    }
  } else {
    inactive_timeout_at = 0;
    if (inactive_event) {
      inactive_event->cancel();
      inactive_event = nullptr;
    }
  }
}

void
PluginVC::cancel_active_timeout()
{
  set_active_timeout(0);
}

void
PluginVC::cancel_inactivity_timeout()
{
  set_inactivity_timeout(0);
}

ink_hrtime
PluginVC::get_active_timeout()
{
  return active_timeout;
}

ink_hrtime
PluginVC::get_inactivity_timeout()
{
  return inactive_timeout;
}

void
PluginVC::add_to_keep_alive_queue()
{
  // do nothing
}

void
PluginVC::remove_from_keep_alive_queue()
{
  // do nothing
}

bool
PluginVC::add_to_active_queue()
{
  // do nothing
  return false;
}

SOCKET
PluginVC::get_socket()
{
  // Return an invalid file descriptor
  return ts::NO_FD;
}

void
PluginVC::set_local_addr()
{
  if (vc_type == PLUGIN_VC_ACTIVE) {
    ats_ip_copy(&local_addr, &core_obj->active_addr_struct);
    //    local_addr = core_obj->active_addr_struct;
  } else {
    ats_ip_copy(&local_addr, &core_obj->passive_addr_struct);
    //    local_addr = core_obj->passive_addr_struct;
  }
}

void
PluginVC::set_remote_addr()
{
  if (vc_type == PLUGIN_VC_ACTIVE) {
    ats_ip_copy(&remote_addr, &core_obj->passive_addr_struct);
  } else {
    ats_ip_copy(&remote_addr, &core_obj->active_addr_struct);
  }
}

void
PluginVC::set_remote_addr(const sockaddr * /* new_sa ATS_UNUSED */)
{
  return;
}

void
PluginVC::set_mptcp_state()
{
  return;
}

int
PluginVC::set_tcp_init_cwnd(int /* init_cwnd ATS_UNUSED */)
{
  return -1;
}

int
PluginVC::set_tcp_congestion_control(int ATS_UNUSED)
{
  return -1;
}

void
PluginVC::apply_options()
{
  // do nothing
}

bool
PluginVC::get_data(int id, void *data)
{
  if (data == nullptr) {
    return false;
  }
  switch (id) {
  case PLUGIN_VC_DATA_LOCAL:
    if (vc_type == PLUGIN_VC_ACTIVE) {
      *(void **)data = core_obj->active_data;
    } else {
      *(void **)data = core_obj->passive_data;
    }
    return true;
  case PLUGIN_VC_DATA_REMOTE:
    if (vc_type == PLUGIN_VC_ACTIVE) {
      *(void **)data = core_obj->passive_data;
    } else {
      *(void **)data = core_obj->active_data;
    }
    return true;
  case TS_API_DATA_CLOSED:
    *static_cast<int *>(data) = this->closed;
    return true;
  default:
    *(void **)data = nullptr;
    return false;
  }
}

bool
PluginVC::set_data(int id, void *data)
{
  switch (id) {
  case PLUGIN_VC_DATA_LOCAL:
    if (vc_type == PLUGIN_VC_ACTIVE) {
      core_obj->active_data = data;
    } else {
      core_obj->passive_data = data;
    }
    return true;
  case PLUGIN_VC_DATA_REMOTE:
    if (vc_type == PLUGIN_VC_ACTIVE) {
      core_obj->passive_data = data;
    } else {
      core_obj->active_data = data;
    }
    return true;
  default:
    return false;
  }
}

// PluginVCCore

int32_t PluginVCCore::nextid;

PluginVCCore::~PluginVCCore() {}

PluginVCCore *
PluginVCCore::alloc(Continuation *acceptor)
{
  PluginVCCore *pvc = new PluginVCCore;
  pvc->init();
  pvc->connect_to = acceptor;
  return pvc;
}

void
PluginVCCore::init()
{
  mutex = new_ProxyMutex();

  active_vc.vc_type    = PLUGIN_VC_ACTIVE;
  active_vc.other_side = &passive_vc;
  active_vc.core_obj   = this;
  active_vc.mutex      = mutex;
  active_vc.thread     = this_ethread();

  passive_vc.vc_type    = PLUGIN_VC_PASSIVE;
  passive_vc.other_side = &active_vc;
  passive_vc.core_obj   = this;
  passive_vc.mutex      = mutex;
  passive_vc.thread     = active_vc.thread;

  p_to_a_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  p_to_a_reader = p_to_a_buffer->alloc_reader();

  a_to_p_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  a_to_p_reader = a_to_p_buffer->alloc_reader();

  Debug("pvc", "[%u] Created PluginVCCore at %p, active %p, passive %p", id, this, &active_vc, &passive_vc);
}

void
PluginVCCore::destroy()
{
  Debug("pvc", "[%u] Destroying PluginVCCore at %p", id, this);

  ink_assert(active_vc.closed == true || !connected);
  active_vc.mutex = nullptr;
  active_vc.read_state.vio.buffer.clear();
  active_vc.write_state.vio.buffer.clear();
  active_vc.magic = PLUGIN_VC_MAGIC_DEAD;

  ink_assert(passive_vc.closed == true || !connected);
  passive_vc.mutex = nullptr;
  passive_vc.read_state.vio.buffer.clear();
  passive_vc.write_state.vio.buffer.clear();
  passive_vc.magic = PLUGIN_VC_MAGIC_DEAD;

  if (p_to_a_buffer) {
    free_MIOBuffer(p_to_a_buffer);
    p_to_a_buffer = nullptr;
  }

  if (a_to_p_buffer) {
    free_MIOBuffer(a_to_p_buffer);
    a_to_p_buffer = nullptr;
  }

  this->mutex = nullptr;
  delete this;
}

PluginVC *
PluginVCCore::connect()
{
  ink_release_assert(connect_to != nullptr);

  connected = true;
  state_send_accept(EVENT_IMMEDIATE, nullptr);

  return &active_vc;
}

Action *
PluginVCCore::connect_re(Continuation *c)
{
  ink_release_assert(connect_to != nullptr);

  EThread *my_thread = this_ethread();
  MUTEX_TAKE_LOCK(this->mutex, my_thread);

  connected = true;
  state_send_accept(EVENT_IMMEDIATE, nullptr);

  // We have to take out our mutex because rest of the
  //   system expects the VC mutex to held when calling back.
  // We can use take lock here instead of try lock because the
  //   lock should never already be held.

  c->handleEvent(NET_EVENT_OPEN, &active_vc);
  MUTEX_UNTAKE_LOCK(this->mutex, my_thread);

  return ACTION_RESULT_DONE;
}

int
PluginVCCore::state_send_accept_failed(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (connect_to->mutex == nullptr) {
    connect_to->handleEvent(NET_EVENT_ACCEPT_FAILED, nullptr);
    destroy();
  } else {
    MUTEX_TRY_LOCK(lock, connect_to->mutex, this_ethread());

    if (lock.is_locked()) {
      connect_to->handleEvent(NET_EVENT_ACCEPT_FAILED, nullptr);
      destroy();
    } else {
      SET_HANDLER(&PluginVCCore::state_send_accept_failed);
      eventProcessor.schedule_in(this, PVC_LOCK_RETRY_TIME);
    }
  }

  return 0;
}

int
PluginVCCore::state_send_accept(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  if (connect_to->mutex == nullptr) {
    connect_to->handleEvent(NET_EVENT_ACCEPT, &passive_vc);
  } else {
    MUTEX_TRY_LOCK(lock, connect_to->mutex, this_ethread());

    if (lock.is_locked()) {
      connect_to->handleEvent(NET_EVENT_ACCEPT, &passive_vc);
    } else {
      SET_HANDLER(&PluginVCCore::state_send_accept);
      eventProcessor.schedule_in(this, PVC_LOCK_RETRY_TIME);
    }
  }

  return 0;
}

// void PluginVCCore::attempt_delete()
//
//  Mutex must be held when calling this function
//
void
PluginVCCore::attempt_delete()
{
  if (active_vc.deletable) {
    if (passive_vc.deletable) {
      destroy();
    } else if (!connected) {
      state_send_accept_failed(EVENT_IMMEDIATE, nullptr);
    }
  }
}

// void PluginVCCore::kill_no_connect()
//
//   Called to kill the PluginVCCore when the
//     connect call hasn't been made yet
//
void
PluginVCCore::kill_no_connect()
{
  ink_assert(!connected);
  ink_assert(!active_vc.closed);
  active_vc.do_io_close();
}

void
PluginVCCore::set_passive_addr(in_addr_t ip, int port)
{
  ats_ip4_set(&passive_addr_struct, htonl(ip), htons(port));
}

void
PluginVCCore::set_passive_addr(sockaddr const *ip)
{
  passive_addr_struct.assign(ip);
}

void
PluginVCCore::set_active_addr(in_addr_t ip, int port)
{
  ats_ip4_set(&active_addr_struct, htonl(ip), htons(port));
}

void
PluginVCCore::set_active_addr(sockaddr const *ip)
{
  active_addr_struct.assign(ip);
}

void
PluginVCCore::set_passive_data(void *data)
{
  passive_data = data;
}

void
PluginVCCore::set_active_data(void *data)
{
  active_data = data;
}

void
PluginVCCore::set_transparent(bool passive_side, bool active_side)
{
  passive_vc.set_is_transparent(passive_side);
  active_vc.set_is_transparent(active_side);
}

void
PluginVCCore::set_plugin_id(int64_t id)
{
  passive_vc.plugin_id = active_vc.plugin_id = id;
}

void
PluginVCCore::set_plugin_tag(const char *tag)
{
  passive_vc.plugin_tag = active_vc.plugin_tag = tag;
}

/*************************************************************
 *
 *   REGRESSION TEST STUFF
 *
 **************************************************************/

#if TS_HAS_TESTS
class PVCTestDriver : public NetTestDriver
{
public:
  PVCTestDriver();
  ~PVCTestDriver() override;

  void start_tests(RegressionTest *r_arg, int *pstatus_arg);
  void run_next_test();
  int main_handler(int event, void *data);

private:
  unsigned i                    = 0;
  unsigned completions_received = 0;
};

PVCTestDriver::PVCTestDriver() : NetTestDriver() {}

PVCTestDriver::~PVCTestDriver()
{
  mutex = nullptr;
}

void
PVCTestDriver::start_tests(RegressionTest *r_arg, int *pstatus_arg)
{
  mutex = new_ProxyMutex();
  MUTEX_TRY_LOCK(lock, mutex, this_ethread());

  r       = r_arg;
  pstatus = pstatus_arg;
  SET_HANDLER(&PVCTestDriver::main_handler);

  run_next_test();
}

void
PVCTestDriver::run_next_test()
{
  unsigned a_index = i * 2;
  unsigned p_index = a_index + 1;

  if (p_index >= num_netvc_tests) {
    // We are done - // FIX - PASS or FAIL?
    if (errors == 0) {
      *pstatus = REGRESSION_TEST_PASSED;
    } else {
      *pstatus = REGRESSION_TEST_FAILED;
    }
    delete this;
    return;
  }
  completions_received = 0;
  i++;

  Debug("pvc_test", "Starting test %s", netvc_tests_def[a_index].test_name);

  NetVCTest *p       = new NetVCTest;
  NetVCTest *a       = new NetVCTest;
  PluginVCCore *core = PluginVCCore::alloc(p);

  p->init_test(NET_VC_TEST_PASSIVE, this, nullptr, r, &netvc_tests_def[p_index], "PluginVC", "pvc_test_detail");
  PluginVC *a_vc = core->connect();

  a->init_test(NET_VC_TEST_ACTIVE, this, a_vc, r, &netvc_tests_def[a_index], "PluginVC", "pvc_test_detail");
}

int
PVCTestDriver::main_handler(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  completions_received++;

  if (completions_received == 2) {
    run_next_test();
  }

  return 0;
}

EXCLUSIVE_REGRESSION_TEST(PVC)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  PVCTestDriver *driver = new PVCTestDriver;
  driver->start_tests(t, pstatus);
}
#endif
