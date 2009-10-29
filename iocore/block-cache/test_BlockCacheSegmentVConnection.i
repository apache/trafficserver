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

#include "diags.i"

#include "I_AIO.h"

#define DEFAULT_BUFFER_INDEX BUFFER_SIZE_INDEX_32K
#define NBLOCKS 5

class Test1:public Continuation
{
public:
  Test1()
  :Continuation(new_ProxyMutex())
  , m_vc(NULL)
  {
    m_vc = BlockCacheSegmentVConnection_util::create(mutex);
    m_buf = new_MIOBuffer(DEFAULT_BUFFER_INDEX);
    m_buf->water_mark = NBLOCKS * 32 * 1024;
    m_reader = m_buf->alloc_reader();
    int nblocks = NBLOCKS;
    while (nblocks > 0)
    {
      char buf[32 * 1024];
        nblocks--;
        m_buf->write(buf, sizeof(buf));
    }

    SET_HANDLER(handleStart);
    eventProcessor.schedule_in(this, HRTIME_MSECONDS(1000));
  };
  virtual ~ Test1() {
  };
  int handleStart(int event, void *data)
  {
    printf("got %d,%d\n", event, data);
    // make request
    int len = m_reader->read_avail();
    int offset = 0;
    SET_HANDLER(handleDoneWrite);
    m_vc->do_io_write(this, len, m_reader);
    return EVENT_DONE;
  };
  int handleDoneWrite(int event, void *data)
  {
    printf("got %d,%d\n", event, data);
    switch (event) {
    case VC_EVENT_WRITE_READY:
      // fill in more data
      break;
    case VC_EVENT_WRITE_COMPLETE:
      // done
      break;
    }
    return EVENT_DONE;
  };

private:
  MIOBuffer * m_buf;
  IOBufferReader *m_reader;
  BlockCacheSegmentVConnection *m_vc;
};

int
main(int argc, char *argv[])
{
  (void) argc;
  (void) argv;
  int i;
  int num_net_threads = ink_number_of_processors();
  printf("starting %d net threads\n", num_net_threads);
  RecProcessInit(RECM_STAND_ALONE);
  ink_event_system_init(EVENT_SYSTEM_MODULE_VERSION);
  eventProcessor.start(num_net_threads);
  RecProcessStart();
  ink_aio_init(AIO_MODULE_VERSION);
  srand48(time(NULL));

  Test1 *t = NEW(new Test1);

  this_thread()->execute();
}
