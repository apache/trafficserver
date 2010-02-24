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



#include "I_BlockCacheSegmentVConnection.h"

BlockCacheSegmentVConnection::BlockCacheSegmentVConnection(ProxyMutex * p)
:VConnection(p)
{
}

BlockCacheSegmentVConnection::~BlockCacheSegmentVConnection()
{
}

class BCSV_impl:public BlockCacheSegmentVConnection
{
public:
  BCSV_impl(ProxyMutex * p, int fd);
    virtual ~ BCSV_impl();
  virtual Action *sync(Continuation * c)
  {
    return NULL;
  };
  virtual void setBCOpenSegment(BC_OpenSegment * seg, BlockCacheSegmentVConnection::AccessType type)
  {
    m_seg = seg;
    // attach to BC_OpenSegment (under lock) for appropriate access mode
    switch (type) {
    case BlockCacheSegmentVConnection::e_for_read:
      m_seg->registerReader(this);
      break;
    case BlockCacheSegmentVConnection::e_for_write:
      m_seg->registerWriter(this);
      break;
    }
  };

  virtual VIO *do_io_write(Continuation * c = NULL, ink64 nbytes = 0, IOBufferReader * buf = NULL, bool owner = false);

  virtual VIO *do_io_read(Continuation * c, ink64 nbytes, MIOBuffer * buf);

  virtual void do_io_close(int err = -1) {
  };

  virtual int try_do_io_close(int err = -1) {
    return 0;
  };
  int handleCallback(int event, void *data);

private:
  int m_fd;
  VIO m_vio;
  BC_OpenSegment *m_seg;
};

BCSV_impl::BCSV_impl(ProxyMutex * p, int fd)
  :
BlockCacheSegmentVConnection(p)
  ,
m_fd(fd)
{
  SET_HANDLER(&BCSV_impl::handleCallback);
}

BCSV_impl::~BCSV_impl()
{
}

VIO *
BCSV_impl::do_io_write(Continuation * c, ink64 nbytes, IOBufferReader * buf, bool owner)
{

  // call into BC_OpenSegment for io strategy

  // if another reader present, then hot write logic only allows a
  // write when the fastest reader advances.

  m_vio.set_continuation(c);
  m_vio.op = VIO::WRITE;
  m_vio.nbytes = nbytes;
  m_vio.set_reader(buf);

  // update BC_OpenSegment writeDataAvail w/ buf's readAvail.
  // schedule MTInteractor call to readers.

  return &m_vio;
}

VIO *
BCSV_impl::do_io_read(Continuation * c, ink64 nbytes, MIOBuffer * buf)
{
  // call into BC_OpenSegment for io strategy

  m_vio.set_continuation(c);
  m_vio.op = VIO::READ;
  m_vio.nbytes = nbytes;
  m_vio.set_writer(buf);

  // update BC_OpenSegment readAvail w/ buf's writeAvail.
  // schedule MTInteractor call to writer.

  return &m_vio;
}

int
BCSV_impl::handleCallback(int event, void *data)
{
  // handle AIO callback

  /*
     MTInteractor event handling:

     With reader continuation's lock taken:
     .. If writer has data available, then add/advance MIOBuffer block
     .. pointer and call read continuation with VC_EVENT_READ_READY
     .. update read position.

     .. update BC_OpenSegment readAvail w/ buf's writeavail.

     With writer continuation's lock taken:
     .. If any reader has advanced past last maximum position, then
     .. update the maximum position.

     .. possibly schedule full writer block for writing to disk.  advance
     .. writer offset to next block.
     .. Call writer continuation with VC_EVENT_WRITE_READY

     .. update BC_OpenSegment writeDataAvail w/ buf's readAvail.
   */

  return EVENT_CONT;
}


BlockCacheSegmentVConnection *
BlockCacheSegmentVConnection_util::create(ProxyMutex * p, int fd)
{
  BCSV_impl *ret = NEW(new BCSV_impl(p, fd));
  return ret;
}
