/** @file

  Implements unit test for SDK APIs

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

/*
Test TS API functions:
  TSNetAccept
  TSNetConnect
  TSPortDescriptorParse
  TSPortDescriptorAccept

No data is sent on connections established using TSPortDescriptorAccept().

NOTE: no include guard needed, only included once in core_ready.h.
*/

#include <memory>
#include <atomic>
#include <optional>
#include <sstream>

#include <netinet/in.h>

#include <ts/ts.h>
#include <tscpp/api/Cleanup.h>

#pragma GCC diagnostic push
#ifdef __gcc__
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

using CoreReadyHook::InProgress;

namespace Test_TS_TCP
{
// Make sure a pointer isn't null before using it in an expression.
//
template <typename T>
T *
nonNullPtr(T *ptr)
{
  TSReleaseAssert(ptr != nullptr);

  return ptr;
}

// Base class with mutex that needs to be passed to succeeding base classes.
//
class MutexShim
{
protected:
  MutexShim() : _mtx(nonNullPtr(TSMutexCreate())) {}

  TSMutex
  _mtxGet() const
  {
    return _mtx;
  }

private:
  // Due to TS API bug, this mutex has to be leaked because it's used as a continuation mutex.
  //
  TSMutex _mtx;
};

// C++ wrapper class for TSIOBufferReader.  Note that I/O buffers are not thread-safe.  The user code must
// ensure that there is mutual exclusion of access to an I/O buffer and its readers.
//
class IOBufferConsume
{
public:
  IOBufferConsume() {}

  // Note: user code must destroy all instances referring to a TSIOBuffer before destroying the TSIOBuffer.
  //
  explicit IOBufferConsume(TSIOBuffer io_buffer)
  {
    TSReleaseAssert(io_buffer != nullptr);
    auto p = TSIOBufferReaderAlloc(io_buffer);
    TSReleaseAssert(p != nullptr);
    _io_buffer_reader.reset(p);
  }

  IOBufferConsume(IOBufferConsume &&) = default;
  IOBufferConsume &operator=(IOBufferConsume &&) = default;

  // Returns true if associated with a TSIOBuffer.
  //
  bool
  attached() const
  {
    return _io_buffer_reader.get() != nullptr;
  }

  // Returns number of bytes available to consume.
  //
  std::int64_t
  avail()
  {
    if (!_block_avail) {
      if (_block_size) {
        // There is a current block, but it has been used up.  Consume the current block, and get the next one
        // if it's available.

        auto a = TSIOBufferReaderAvail(_io_buffer_reader.get()) - _block_size;
        TSIOBufferReaderConsume(_io_buffer_reader.get(), _block_size);
        TSReleaseAssert(TSIOBufferReaderAvail(_io_buffer_reader.get()) >= a);
        TSReleaseAssert(TSIOBufferReaderStart(_io_buffer_reader.get()) == _io_block);
        _block_size = 0;

        _io_block = TSIOBufferBlockNext(_io_block);
      } else {
        // No current block.  See if one is available.
        //
        _io_block = TSIOBufferReaderStart(_io_buffer_reader.get());
      }
      if (_io_block) {
        // There is a new current block.
        //
        _block_data  = TSIOBufferBlockReadStart(_io_block, _io_buffer_reader.get(), &_block_size);
        _block_avail = _block_size;
      } else {
        // There is no current block.
        //
        _block_size = 0;
      }
    }
    return _block_avail;
  }

  // Consume the data in the returned buffer, of (positive) length "amount".  "amount" must not be greater than
  // avail().  The buffer remains valid until the next call to avail() for this object, or this object is
  // destroyed.  Returns null if avail() is zero (because no data is currently available).  Must not be called if
  // attached returns false.
  //
  char const *
  consume(std::int64_t amount)
  {
    TSReleaseAssert(attached());
    TSReleaseAssert(amount > 0);
    TSReleaseAssert(amount <= _block_avail);

    if (!_block_avail) {
      return nullptr;
    }
    char const *result = _block_data;
    _block_data += amount;
    _block_avail -= amount;

    return result;
  }

private:
  atscppapi::TSIOBufferReaderUniqPtr _io_buffer_reader;

  // If there is no current block (_io_block == nullptr), _block_size and _block_avail are both 0.  If there is a
  // current block, _block_size is it's size, and _block_avail is the number of bytes at the end of the block
  // not yet consumed (less than or equal to _block_size, may be 0).
  //
  std::int64_t _block_size{0}, _block_avail{0};
  TSIOBufferBlock _io_block{nullptr}; // Initialize to shut up compiler warning.
  char const *_block_data{nullptr};   // Initialize to shut up compiler warning.
};

// Continuation that calls a (non-static) member function.  The instance to call it on is passed to the
// constructor and saved as the continuation data.
//
template <class C, int (C::*MbrFuncToCall)(TSEvent, void *)> class MbrFuncCallingCont
{
public:
  explicit MbrFuncCallingCont(C *inst, TSMutex mtx = nullptr)
  {
    TSReleaseAssert(inst != nullptr);

    _cont = TSContCreate(_cont_func, mtx);

    TSReleaseAssert(_cont != nullptr);

    auto this_thread = TSEventThreadSelf();

    TSReleaseAssert(this_thread != nullptr);

    TSContDataSet(_cont, inst);
  }

  // No copying/moving.
  //
  MbrFuncCallingCont(MbrFuncCallingCont const &) = delete;
  MbrFuncCallingCont &operator=(MbrFuncCallingCont const &) = delete;

  ~MbrFuncCallingCont() { TSContDestroy(_cont); }

  TSCont
  cont() const
  {
    return _cont;
  }

  // Object that the member function will be called for when the continuation is triggered.
  //
  C *
  obj()
  {
    return static_cast<C *>(_cont ? TSContDataGet(_cont) : nullptr);
  }

private:
  TSCont _cont{nullptr};

  static int
  _cont_func(TSCont cont_, TSEvent event, void *data)
  {
    void *cp = TSContDataGet(cont_);

    TSReleaseAssert(cp != nullptr);

    return (static_cast<C *>(cp)->*MbrFuncToCall)(event, data);
  }
};

// Consume data coming from a VConnection until end of stream.  There can only be one instance of this class
// associated with a VConnection.
//
class VConnConsume
{
public:
  explicit VConnConsume(TSVConn vconn, TSMutex mtx = nullptr) : _vconn(nonNullPtr(vconn)), _cont(this, mtx)
  {
    auto io_buffer = TSIOBufferCreate();
    TSReleaseAssert(io_buffer != nullptr);
    _io_buffer.reset(io_buffer);

    bc = IOBufferConsume(io_buffer);

    // Set this before starting the VIO, in case _vioHandler() is actually triggered inside TSVConnRead().
    //
    _active = true;

    // Note that the VConn implementor is required to lock the mutex of the given continuation when writing data
    // to the given I/O buffer reader.  The same mutex must be locked when consuming data from a reader associated
    // with the I/O buffer.
    //
    TSReleaseAssert(TSVConnRead(vconn, _cont.cont(), io_buffer, INT64_MAX) != nullptr);
  }

  // Returns number of bytes available to consume.
  //
  std::int64_t
  avail()
  {
    TSReleaseAssert(bc.attached());

    return bc.avail();
  }

  // Consume the data in the returned buffer, of (positive) length "amount".  "amount" must not be greater than
  // avail().  The buffer remains valid until the next call to avail() for this object, or this object is
  // destroyed.  Returns null if avail() is zero (because no data is currently available).
  //
  char const *
  consume(std::int64_t amount)
  {
    TSReleaseAssert(bc.attached());

    return bc.consume(amount);
  }

  // Returns true if instance is active.
  //
  bool
  active() const
  {
    return _active;
  }

private:
  int
  _vioHandler(TSEvent event, void *edata)
  {
    TSDebug(PIName, "VConnConsume Event=%u this=%p", static_cast<unsigned>(event), this);

    TSReleaseAssert(_active);
    TSReleaseAssert(edata != nullptr);

    switch (event) {
    // My best guess is that this event is triggered when the I/O buffer (that is, the one associated with "bc")
    // makes a transition from being empty to being non-empty.
    //
    case TS_EVENT_VCONN_READ_READY: {
      readWakeup();

    } break;

    // My best guess is that this event is triggered when the source of data feeding into the I/O buffer has
    // closed without any known error, but before the end of the active read VIO.
    //
    case TS_EVENT_VCONN_EOS: {
      _active = false;

      readWakeup();

    } break;

    case TS_EVENT_ERROR: {
      TSError(PINAME ": VConnection read error");
      TSReleaseAssert(false);

    } break;

    // My best guess is that this event is triggered when all the bytes reqeusted in the read VIO have been received.
    // This should not happen for this class because the number bytes for the read VIO was INT64_MAX, effectively
    // infinite.
    //
    case TS_EVENT_VCONN_READ_COMPLETE: {
      TSError(PINAME ": VConnection read error -- complete of read VIO with size INT64_MAX");
      TSReleaseAssert(false);

    } break;

    default: {
      TSError(PINAME ": VIO read unknown event: %u", static_cast<unsigned>(event));
      TSReleaseAssert(false);

    } break;
    }
    return 0;
  }

  const TSVConn _vconn;
  MbrFuncCallingCont<VConnConsume, &VConnConsume::_vioHandler> _cont;
  atscppapi::TSIOBufferUniqPtr _io_buffer;
  IOBufferConsume bc; // Order is important here, bc must be destroyed before _io_buffer.
  std::atomic<bool> _active{false};

  // This is called to indicate a possible change in status of the VConnection.  If a mutex was passed to
  // the constructor, it will be locked while this function is executing.
  //
  virtual void
  readWakeup()
  {
  }
};

// Write data to a VConnection.  There can only be one instance of this class assosiated with a VConnection.
//
class VConnWriter
{
public:
  explicit VConnWriter(TSVConn vconn, TSMutex mtx = nullptr) : _vconn(nonNullPtr(vconn)), _cont(this, mtx)
  {
    auto io_buffer = TSIOBufferCreate();
    TSReleaseAssert(io_buffer != nullptr);
    _io_buffer.reset(io_buffer);

    auto io_buffer_reader = TSIOBufferReaderAlloc(io_buffer);
    TSReleaseAssert(io_buffer_reader != nullptr);
    _io_buffer_reader.reset(io_buffer_reader);
  }

  void
  write(void const *data_, std::int64_t size)
  {
    TSReleaseAssert(data_ != nullptr);
    TSReleaseAssert(size > 0);
    TSReleaseAssert(!_closed);

    auto data = static_cast<char const *>(data_);

    do {
      auto size_written = TSIOBufferWrite(_io_buffer.get(), data, size);
      TSReleaseAssert(size_written > 0);
      TSReleaseAssert(size_written <= size);
      size -= size_written;
      data += size_written;
    } while (size);

    _start_vio_if_needed();
  }

  void
  close()
  {
    TSReleaseAssert(!_closed);

    _closed = true;

    _start_vio_if_needed();
  }

  bool
  isClosed() const
  {
    return _closed;
  }

  // Returns true if instance still has data to write (active write VIO).
  //
  bool
  active() const
  {
    return _active;
  }

private:
  void
  _start_vio_if_needed()
  {
    // If write() or close() are calling this at the same time it's being called by _vioHandler(), make sure
    // only one of them starts a new write VIO.
    //
    bool expected{false};
    if (_active.compare_exchange_strong(expected, true)) {
      auto avail = TSIOBufferReaderAvail(_io_buffer_reader.get());
      if (avail) {
        // Note that the VConn implementor is required to lock the mutex of the given continuation when reading data
        // with the given I/O buffer reader.  The same mutex must be locked when writting into the I/O buffer being
        // read.
        //
        TSReleaseAssert(TSVConnWrite(_vconn, _cont.cont(), _io_buffer_reader.get(), avail) != nullptr);

      } else {
        _active = false;
      }
    }
  }

  int
  _vioHandler(TSEvent event, void *edata)
  {
    TSDebug(PIName, "VConnWriter Event=%u this=%p", static_cast<unsigned>(event), this);

    TSReleaseAssert(_active);
    TSReleaseAssert(edata != nullptr); // VIO pointer should not be null.

    switch (event) {
    // My best guess is that this is (only) triggered when the write VIO finishes.
    //
    case TS_EVENT_VCONN_WRITE_COMPLETE: {
      _active = false;

      _start_vio_if_needed();

      if (!_active) {
        writeWakeup();
      }
    } break;

    // My best guess is that this is only triggered when the I/O buffer is empty but the write VIO has not yet written
    // all the bytes it was supposed to.  That should never happpen for the VIOs started by this class, because the
    // size of the VIO is the number of bytes in the I/O buffer.
    //
    case TS_EVENT_VCONN_WRITE_READY: {
      TSError(PINAME ": VConnection write ready event");
      TSReleaseAssert(false);

      // My best guess is that a write VIO must be reenabled (only) after this event.  Reenable seems to be
      // useless and unnecessary for read VIOs.
      //
      // TSVIOReenable(vio);

    } break;

    // My best guess is that this is (only) triggered when the VConnection shuts down for writing in a
    // non-error case, while there is an active write VIO.
    //
    case TS_EVENT_VCONN_EOS: {
      TSError(PINAME ": VConnection write EOS event");
      TSReleaseAssert(false);

    } break;

    case TS_EVENT_ERROR: {
      TSError(PINAME ": VConnection write error");
      TSReleaseAssert(false);

    } break;

    default: {
      TSError(PINAME ": VIO write unknown event: %u", static_cast<unsigned>(event));
      TSReleaseAssert(false);

    } break;
    }
    return 0;
  }

  std::atomic<bool> _active{false}, _closed{false};
  const TSVConn _vconn;
  MbrFuncCallingCont<VConnWriter, &VConnWriter::_vioHandler> _cont;
  atscppapi::TSIOBufferUniqPtr _io_buffer;
  atscppapi::TSIOBufferReaderUniqPtr _io_buffer_reader; // order matters, this must be destroyed before _io_buffer.

  // This is called to indicate that there are no current pending bytes to write.
  //
  virtual void
  writeWakeup()
  {
  }
};

// Write all data received on a VConnection back to the same VConnection.  Instances must be in heap, they
// delete thenselves when an EOS event occures on VConnection write.
//
class VConnLoopback final : private InProgress, private MutexShim, public VConnConsume, public VConnWriter
{
public:
  VConnLoopback(TSVConn vconn, InProgress ip, TSCont optional_destruct_cont = nullptr)
    : InProgress(ip),
      VConnConsume(vconn, _mtxGet()),
      VConnWriter(vconn, _mtxGet()),
      _vconn(vconn),
      _optional_destruct_cont{optional_destruct_cont}
  {
    TSDebug(PIName, "VConnLoopback constructor");
  }

  ~VConnLoopback()
  {
    TSDebug(PIName, "VConnLoopback destructor");

    // Calls to TSVConnShutdown() do not seem to be necessary.
    //
    TSVConnClose(_vconn);

    if (_optional_destruct_cont) {
      TSDebug(PIName, "Scheduling optional destruct continuation");
      TSReleaseAssert(TSContScheduleOnPool(_optional_destruct_cont, 0, TS_THREAD_POOL_TASK) != nullptr);
    }
  }

private:
  void
  readWakeup() override
  {
    auto avail_ = avail();

    while (avail_) {
      char const *data = consume(avail_);
      write(data, avail_);
      avail_ = avail();
    }

    if (!VConnConsume::active()) {
      // VConnConsume::active() should only be false when EOS was received for its read VIO.  This shouldn't
      // happen while data is still actively being written back to the VConnection.
      //
      TSReleaseAssert(!VConnWriter::active());

      delete this;
    }
  }

  const TSVConn _vconn;
  const TSCont _optional_destruct_cont;
};

// Accept TCP connections and start loopbacks on the associated VConnections for them.
//
class TCPoIPv4LoopbackServer
{
public:
  // It seems to be a TS API bug that you must leak any mutex used as a continuation mutex.
  //
  TCPoIPv4LoopbackServer(unsigned tcp_port_num, InProgress ip, TSCont use_port_descriptor_cont)
    : _use_port_descriptor_cont(use_port_descriptor_cont), _ip(ip), _cont(this, nullptr)
  {
    if (use_port_descriptor_cont) {
      // Test failure case first.
      //
      auto pdesc = TSPortDescriptorParse(nullptr);
      TSReleaseAssert(pdesc == nullptr);

      // Test success case.
      //
      std::ostringstream oss;
      oss << tcp_port_num;
      pdesc = TSPortDescriptorParse(oss.str().c_str());
      TSReleaseAssert(TSPortDescriptorAccept(pdesc, _cont.cont()) == TS_SUCCESS);

    } else {
      _action = TSNetAccept(_cont.cont(), tcp_port_num, AF_INET, 0);
      TSReleaseAssert(_action != nullptr);
    }
  }

  ~TCPoIPv4LoopbackServer()
  {
    if (!_use_port_descriptor_cont) {
      // Note:  it seems that, if you schedule immediate a continuation, and call TSActionDone() on the returned action
      // in the continuation function, it will return false.  It also seems that if you call TSActionCancel() on
      // the contination's action in its function, this will cause an assert.

      TSReleaseAssert(!TSActionDone(_action));
      TSActionCancel(_action);
    }
  }

  bool
  use_port_descriptor() const
  {
    return _use_port_descriptor_cont != nullptr;
  }

private:
  int
  _contFunc(TSEvent event, void *data)
  {
    TSDebug(PIName, "TCPoIPv4LoopbackServer this=%p Event=%u", this, static_cast<unsigned>(event));

    TSReleaseAssert(nullptr != data);

    switch (event) {
    case TS_EVENT_NET_ACCEPT: {
      // Create self-deleting loopback server object in heap.
      //
      static_cast<void>(new VConnLoopback(static_cast<TSVConn>(data), _ip, _use_port_descriptor_cont));
    } break;

    case TS_EVENT_NET_ACCEPT_FAILED: {
      TSError(PINAME ": TS_EVENT_NET_ACCEPT_FAILED");
      TSReleaseAssert(false);
    } break;

    default: {
      TSError(PINAME ": TSNetAccept unknown event: %u", static_cast<unsigned>(event));
      TSReleaseAssert(false);
    } break;
    }
    return 0;
  }

  using _SelfT = TCPoIPv4LoopbackServer;

  TSCont _use_port_descriptor_cont{nullptr};
  InProgress _ip;
  MbrFuncCallingCont<_SelfT, &_SelfT::_contFunc> _cont;
  TSAction _action;
};

// Write a pattern on a VConnection and check that it comes back from the same VConnection.
//
class VConnLoopbackTest final : private MutexShim, private VConnConsume, private VConnWriter
{
public:
  VConnLoopbackTest(TSVConn vconn, TSCont done)
    : VConnConsume(vconn, _mtxGet()), VConnWriter(vconn, _mtxGet()), _vconn(vconn), _done(done)
  {
    TSReleaseAssert(_vconn != nullptr);
    TSReleaseAssert(_done != nullptr);

    TSDebug(PIName, "VConnLoopbackTest constructor");
  }

  // Do test, send count bytes, then trigger _done continuation with event TS_EVENT_IMMEDIATE.
  //
  void
  execute(unsigned count)
  {
    TSReleaseAssert(0 == _left_to_receive);

    _left_to_send    = count;
    _left_to_receive = count;
    _send_byte       = 0;
    _receive_byte    = 0;

    // Write first batch.  Hold continuation mutex to ensure it does not run until _write_batch() exits.
    //
    TSMutexLock(_mtxGet());
    _write_batch();
    TSMutexUnlock(_mtxGet());
  }

  void
  close()
  {
    TSVConnClose(_vconn);
  }

  bool
  active() const
  {
    return VConnConsume::active();
  }

private:
  // Write next batch of bytes in ramping pattern, up to 1000.
  //
  void
  _write_batch()
  {
    for (unsigned i = 0; (i < 1000) && _left_to_send; ++i) {
      write(&_send_byte, 1);
      ++_send_byte;
      --_left_to_send;
    }
  }

  void
  readWakeup() override
  {
    TSReleaseAssert(VConnConsume::active());

    // Consume all available bytes, and make sure they have the ramping pattern that was written to the VConnection.

    auto avail_ = avail();

    while (avail_) {
      char const *data = consume(avail_);
      do {
        if (!_left_to_receive) {
          TSError(PINAME ": VConnLoopbackTest avail_=%u", static_cast<unsigned>(avail_));
          TSReleaseAssert(false);
          return; // dummy for compilation
        }
        if (*(data++) != _receive_byte++) {
          TSError(PINAME ": VConnLoopbackTest data=%d _receive_byte=%d _left_to_receive=%u", *(data - 1), _receive_byte - 1,
                  _left_to_receive);
          TSReleaseAssert(false);
          return; // dummy for compilation
        }
        --avail_;
        --_left_to_receive;
      } while (avail_);

      avail_ = avail();
    }
    if (!_left_to_receive) {
      TSReleaseAssert(TSContScheduleOnPool(_done, 0, TS_THREAD_POOL_TASK) != nullptr);
    }
  }

  void
  writeWakeup() override
  {
    // Ready for more data, so send it.
    //
    _write_batch();
  }

  TSVConn const _vconn;
  TSCont const _done;
  unsigned _left_to_send;
  unsigned _left_to_receive{0};
  char _send_byte, _receive_byte;
};

// Connect to loopback port and test it.  Each instance must be created in heap, it deletes itself when done.
//
class TCPoIPv4LoopbackTester : private InProgress
{
public:
  TCPoIPv4LoopbackTester(unsigned tcp_port_num, InProgress ip, bool use_port_descriptor)
    : _ip(ip), _mtx(TSMutexCreate()), _connect_cont(this, _mtx), _tcp_port_num(tcp_port_num)
  {
    if (use_port_descriptor) {
      _accept_cont.reset(TSContCreate(_acceptContFunc, _mtx));
      TSContDataSet(_accept_cont.get(), this);
    }
    _server.reset(new TCPoIPv4LoopbackServer(tcp_port_num, ip, _accept_cont.get()));

    for (unsigned i = 0; i < NUM_CONNECTIONS; ++i) {
      // The data pointer of the continuation for connection i is a pointer to _done_cont_data[i].  So it
      // *(data pointer) gives a pointer to this object, and then (data pointer) - this->_done_cont_data gives
      // the zero-base index of the connection.
      //
      _done_cont_data[i] = this;
    }

    sockaddr_in addr;
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(tcp_port_num);
    addr.sin_addr.s_addr = htonl((static_cast<std::uint32_t>(127) << (3 * 8)) + 1);

    // Run the same test over multiple (NUM_CONNECTIONS) different TCP connections simultaneously.
    //
    for (unsigned i = 0; i < NUM_CONNECTIONS; ++i) {
      _conn_action[i] = TSNetConnect(_connect_cont.cont(), reinterpret_cast<sockaddr *>(&addr));
      TSReleaseAssert(_conn_action[i] != nullptr);
    }
  }

  ~TCPoIPv4LoopbackTester()
  {
    if (!_server.get()->use_port_descriptor()) {
      TSReleaseAssert(NUM_CONNECTIONS == _conn_count);

      for (unsigned i = 0; i < NUM_CONNECTIONS; ++i) {
        TSReleaseAssert(2 == _per_conn[i].done_count);

        TSReleaseAssert(!TSActionDone(_conn_action[i]));
        TSActionCancel(_conn_action[i]);
      }
    }
  }

private:
  int
  _connectContFunc(TSEvent event, void *data)
  {
    TSDebug(PIName, "TCPoIPv4LoopbackTester this=%p Event=%u", this, static_cast<unsigned>(event));

    switch (event) {
    case TS_EVENT_NET_CONNECT: {
      TSReleaseAssert(data != nullptr);

      auto vconn = static_cast<TSVConn>(data);

      // Test the TS API function to get the remote TCP socket address (which uses the IPv4 loopback interface).
      //
      auto *addr = reinterpret_cast<sockaddr_in const *>(TSNetVConnRemoteAddrGet(vconn));
      TSReleaseAssert(addr != nullptr);
      TSReleaseAssert(AF_INET == addr->sin_family);
      TSReleaseAssert(htonl((static_cast<std::uint32_t>(127) << (3 * 8)) + 1) == addr->sin_addr.s_addr);
      TSReleaseAssert(htons(_tcp_port_num) == addr->sin_port);

      TSReleaseAssert(_conn_count < NUM_CONNECTIONS);

      if (_server.get()->use_port_descriptor()) {
        // Like the Regression Test for port descriptor, don't send any data, just close the connection.
        //
        ++_conn_count;
        TSVConnClose(vconn);
        ++_closed_clients;
        _self_delete_when_done();
        return 0;
      }

      _PerConnection &pc = _per_conn[_conn_count];

      pc.done_cont.reset(TSContCreate(_doneContFunc, _mtx));
      TSReleaseAssert(pc.done_cont.get() != nullptr);

      TSContDataSet(pc.done_cont.get(), _done_cont_data + _conn_count);

      pc.tester.emplace(vconn, pc.done_cont.get());

      TSReleaseAssert(pc.tester.value().active());

      // First test: send 1 byte.
      //
      _per_conn[_conn_count].tester.value().execute(1);

      ++_conn_count;

    } break;

    case TS_EVENT_NET_CONNECT_FAILED: {
      TSError(PINAME ": TS_EVENT_NET_CONNECT_FAILED");
      TSReleaseAssert(false);
    } break;

    default: {
      TSError(PINAME ": TSNetConnect unknown event: %u", static_cast<unsigned>(event));
      TSReleaseAssert(false);
    } break;
    }

    return 0;
  }

  void
  _self_delete_when_done()
  {
    TSDebug(PIName, "TCPoIPv4LoopbackTester this=%p _closed_clients=%u _closed_servers=%u", this, _closed_clients, _closed_servers);

    if ((_closed_clients == NUM_CONNECTIONS) && (!_server.get()->use_port_descriptor() || (_closed_servers == NUM_CONNECTIONS))) {
      TSDebug(PIName, "Deleting TCPoIPv4LoopbackTester address=%p", this);
      delete this;
    }
  }

  static int
  _doneContFunc(TSCont cont, TSEvent event, void *)
  {
    TSReleaseAssert(TS_EVENT_IMMEDIATE == event);

    void *cont_data = TSContDataGet(cont);

    TSReleaseAssert(cont_data != nullptr);

    auto done_cont_data_ptr = static_cast<_SelfT **>(cont_data);

    auto this_ = *done_cont_data_ptr;

    TSReleaseAssert(this_->_server.get() != nullptr);
    TSReleaseAssert(!this_->_server.get()->use_port_descriptor());

    unsigned conn_idx = done_cont_data_ptr - this_->_done_cont_data;

    _PerConnection &pc = this_->_per_conn[conn_idx];

    // A test is done, or the tester has become inactive.

    TSReleaseAssert(pc.tester.has_value());

    ++pc.done_count;

    TSDebug(PIName, "TCPoIPv4LoopbackTester this_=%p done connection=%u done_count=%u", this_, conn_idx, pc.done_count);

    if (1 == pc.done_count) {
      // Finished first test.

      TSReleaseAssert(pc.tester.value().active());

      // Start second test: send 50,000 bytes.
      //
      pc.tester.value().execute(50 * 1000);

    } else if (2 == pc.done_count) {
      // Finished second (and last) test.

      TSReleaseAssert(pc.tester.value().active());

      pc.tester.value().close();

      ++this_->_closed_clients;
      this_->_self_delete_when_done();

    } else {
      TSReleaseAssert(false);
    }

    return 0;
  }

  static int
  _acceptContFunc(TSCont cont, TSEvent event, void *)
  {
    // This is triggered when the TCP connection is a accepted ONLY for the port descriptor test.  This
    // is needed because no data is sent for this test.  For the normal case test, The VCONN tester waits
    // for sent data to be looped back instead.

    TSDebug(PIName, "_acceptContFunc() called");

    TSReleaseAssert(TS_EVENT_IMMEDIATE == event);

    void *cont_data = TSContDataGet(cont);

    TSReleaseAssert(cont_data != nullptr);

    auto this_ = static_cast<_SelfT *>(cont_data);

    ++this_->_closed_servers;
    this_->_self_delete_when_done();

    return 0;
  }

  using _SelfT = TCPoIPv4LoopbackTester;

  static unsigned const NUM_CONNECTIONS{5};

  InProgress _ip;

  // Due to TS API bug, this mutex has to be leaked because it's used as a continuation mutex.
  //
  TSMutex _mtx{nullptr};

  std::unique_ptr<TCPoIPv4LoopbackServer> _server;
  MbrFuncCallingCont<_SelfT, &_SelfT::_connectContFunc> _connect_cont;
  unsigned _conn_count{0}, _closed_clients{0}, _closed_servers{0};
  TSAction _conn_action[NUM_CONNECTIONS];

  struct _PerConnection {
    TSVConn conn{nullptr};
    std::optional<VConnLoopbackTest> tester;
    atscppapi::TSContUniqPtr done_cont;
    unsigned done_count{0};
  };

  _PerConnection _per_conn[NUM_CONNECTIONS];

  _SelfT *_done_cont_data[NUM_CONNECTIONS];

  unsigned _tcp_port_num;

  atscppapi::TSContUniqPtr _accept_cont;
};

void
f(InProgress ip)
{
  // Create tester objects, which will delete themselves.
  //
  static_cast<void>(new TCPoIPv4LoopbackTester(test_tcp_port, ip, false));
  static_cast<void>(new TCPoIPv4LoopbackTester(test_tcp_port2, ip, true));
}

TEST(f)

} // end namespace Test_TS_TCP

#pragma GCC diagnostic pop
