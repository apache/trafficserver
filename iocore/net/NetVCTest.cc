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

   NetVCTest.cc

   Description:
       Unit test for infrastructure for VConnections implementing the
         NetVConnection interface




 ****************************************************************************/

#include "P_Net.h"

// Each test requires two definition entries.  One for the passive
//   side of the connection and one for the active side
//
//  test fields:
//
//  name bytes_to_send nbytes_write bytes_to_read nbytes_read write_per timeout read_term write_term
//
NVC_test_def netvc_tests_def[] = {

  {"basic", 2000, 2000, 2000, 2000, 50, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},
  {"basic", 2000, 2000, 2000, 2000, 50, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},

  {"basic2", 10001, 10001, 5001, 5001, 1024, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},
  {"basic2", 5001, 5001, 10001, 10001, 1024, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},

  {"large", 1000000, 1000000, 500000, 500000, 8192, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},
  {"large", 500000, 500000, 1000000, 1000000, 8192, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},

  // Test large block transfers
  {"larget", 1000000, 1000000, 500000, 500000, 40000, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},
  {"larget", 500000, 500000, 1000000, 1000000, 40000, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},

  {"eos", 4000, 4000, 10, 10, 8192, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},
  {"eos", 10, 10, 6000, 6000, 8192, 10, VC_EVENT_EOS, VC_EVENT_WRITE_COMPLETE},

  {"werr", 4000, 4000, 10, 10, 129, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_ERROR},
  {"werr", 10, 10, 10, 10, 129, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},

  {"itimeout", 6000, 8000, 10, 10, 512, 10, VC_EVENT_READ_COMPLETE, VC_EVENT_INACTIVITY_TIMEOUT},
  {"itimeout", 10, 10, 6000, 8000, 512, 20, VC_EVENT_EOS, VC_EVENT_WRITE_COMPLETE},

  // Test the small transfer code one byte at a time
  {"smallt", 400, 400, 500, 500, 1, 15, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},
  {"smallt", 500, 500, 400, 400, 1, 15, VC_EVENT_READ_COMPLETE, VC_EVENT_WRITE_COMPLETE},

  // The purpose of this test is show that stack can over flow if we move too
  //   small of blocks between the buffers.  EVENT_NONE is wild card error event
  //   since which side gets the timeout is unpredictable
  {"overflow", 1000000, 1000000, 50, 50, 1, 20, VC_EVENT_READ_COMPLETE, EVENT_NONE},
  {"overflow", 50, 50, 0, 35000, 1024, 35, EVENT_NONE, VC_EVENT_WRITE_COMPLETE}

};
const unsigned num_netvc_tests = countof(netvc_tests_def);

NetVCTest::NetVCTest() : Continuation(nullptr) {}

NetVCTest::~NetVCTest()
{
  mutex = nullptr;

  if (read_buffer) {
    Debug(debug_tag, "Freeing read MIOBuffer with %d blocks on %s", read_buffer->max_block_count(),
          (test_cont_type == NET_VC_TEST_ACTIVE) ? "Active" : "Passive");
    free_MIOBuffer(read_buffer);
    read_buffer = nullptr;
  }

  if (write_buffer) {
    Debug(debug_tag, "Freeing write MIOBuffer with %d blocks on %s", write_buffer->max_block_count(),
          (test_cont_type == NET_VC_TEST_ACTIVE) ? "Active" : "Passive");
    free_MIOBuffer(write_buffer);
    write_buffer = nullptr;
  }
}

void
NetVCTest::init_test(NetVcTestType_t c_type, NetTestDriver *driver_arg, NetVConnection *nvc, RegressionTest *robj,
                     NVC_test_def *my_def, const char *module_name_arg, const char *debug_tag_arg)
{
  test_cont_type = c_type;
  driver         = driver_arg;
  test_vc        = nvc;
  regress        = robj;
  module_name    = module_name_arg;
  debug_tag      = debug_tag_arg;

  bytes_to_send = my_def->bytes_to_send;
  bytes_to_read = my_def->bytes_to_read;

  nbytes_read  = my_def->nbytes_read;
  nbytes_write = my_def->nbytes_write;

  write_bytes_to_add_per = my_def->write_bytes_per;
  timeout                = my_def->timeout;
  expected_read_term     = my_def->expected_read_term;
  expected_write_term    = my_def->expected_write_term;
  test_name              = my_def->test_name;

  mutex = new_ProxyMutex();
  SET_HANDLER(&NetVCTest::main_handler);

  if (c_type == NET_VC_TEST_ACTIVE) {
    start_test();
  }
}

void
NetVCTest::start_test()
{
  test_vc->set_inactivity_timeout(HRTIME_SECONDS(timeout));
  test_vc->set_active_timeout(HRTIME_SECONDS(timeout + 5));

  read_buffer  = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
  write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);

  reader_for_rbuf = read_buffer->alloc_reader();
  reader_for_wbuf = write_buffer->alloc_reader();

  if (nbytes_read > 0) {
    read_vio = test_vc->do_io_read(this, nbytes_read, read_buffer);
  } else {
    read_done = true;
  }

  if (nbytes_write > 0) {
    write_vio = test_vc->do_io_write(this, nbytes_write, reader_for_wbuf);
  } else {
    write_done = true;
  }
}

int
NetVCTest::fill_buffer(MIOBuffer *buf, uint8_t *seed, int bytes)
{
  char *space = static_cast<char *>(ats_malloc(bytes));
  char *tmp   = space;
  int to_add  = bytes;

  while (bytes > 0) {
    *tmp = *seed;
    (*seed)++;
    bytes--;
    tmp++;
  }

  buf->write(space, to_add);
  ats_free(space);

  return to_add;
}

int
NetVCTest::consume_and_check_bytes(IOBufferReader *r, uint8_t *seed)
{
  uint8_t *tmp, *end;
  int b_consumed = 0;

  if (actual_bytes_read >= bytes_to_read) {
    return 1;
  }

  while (r->read_avail() > 0) {
    int64_t b_avail = r->block_read_avail();

    tmp        = reinterpret_cast<uint8_t *>(r->start());
    end        = tmp + b_avail;
    b_consumed = 0;

    while (tmp < end && actual_bytes_read < bytes_to_read) {
      actual_bytes_read++;
      b_consumed++;
      if (*tmp != *seed) {
        r->consume(b_consumed);
        return 0;

      } else {
        tmp++;
        (*seed)++;
      }
    }

    Debug(debug_tag, "consume_&_check: read %d, to_read %d", actual_bytes_read, bytes_to_read);
    r->consume(b_consumed);
  }

  return 1;
}

void
NetVCTest::write_finished()
{
  if (nbytes_write != write_vio->ndone && expected_write_term == VC_EVENT_WRITE_COMPLETE) {
    record_error("write: bad ndone value");
    return;
  }

  write_done = true;

  if (read_done) {
    test_vc->do_io_close();
    finished();
  } else {
    test_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
  }
}

void
NetVCTest::read_finished()
{
  if (nbytes_read != read_vio->ndone && expected_read_term != VC_EVENT_EOS && expected_read_term != VC_EVENT_NONE) {
    record_error("read: bad ndone value");
    return;
  }

  read_done = true;

  if (write_done) {
    test_vc->do_io_close();
    finished();
  } else {
    test_vc->do_io_shutdown(IO_SHUTDOWN_READ);
  }
}

void
NetVCTest::record_error(const char *msg)
{
  rprintf(regress, "  %s test: %s failed : %s : on %s\n", module_name, test_name, msg,
          (test_cont_type == NET_VC_TEST_ACTIVE) ? "Active" : "Passive");
  ink_atomic_increment(&driver->errors, 1);

  test_vc->do_io_close();
  finished();
}

void
NetVCTest::finished()
{
  eventProcessor.schedule_imm(driver);
  delete this;
}

void
NetVCTest::write_handler(int event)
{
  Debug(debug_tag, "write_handler received event %d on %s", event, (test_cont_type == NET_VC_TEST_ACTIVE) ? "Active" : "Passive");

  switch (event) {
  case VC_EVENT_WRITE_READY:
    if (write_vio->ndone < bytes_to_send) {
      int left_to_send = bytes_to_send - actual_bytes_sent;
      ink_assert(left_to_send >= 0);
      int to_fill = std::min(left_to_send, write_bytes_to_add_per);
      actual_bytes_sent += fill_buffer(write_buffer, &write_seed, to_fill);
      write_vio->reenable();
    }
    break;
  case VC_EVENT_WRITE_COMPLETE:
    write_finished();
    break;
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
    if (expected_write_term != event && expected_write_term != VC_EVENT_NONE) {
      record_error("write: Unexpected error or timeout");
    } else {
      write_finished();
    }
    break;
  default:
    record_error("write: Unknown event");
    break;
  }
}

void
NetVCTest::read_handler(int event)
{
  Debug(debug_tag, "read_handler received event %d on %s", event, (test_cont_type == NET_VC_TEST_ACTIVE) ? "Active" : "Passive");

  switch (event) {
  case VC_EVENT_READ_READY:
    if (consume_and_check_bytes(reader_for_rbuf, &read_seed) == 0) {
      record_error("Read content corrupt");
      return;
    } else {
      read_vio->reenable();
    }
    break;
  case VC_EVENT_READ_COMPLETE:
    if (consume_and_check_bytes(reader_for_rbuf, &read_seed) == 0) {
      record_error("Read content corrupt");
      return;
    } else {
      read_finished();
    }
    break;
  case VC_EVENT_EOS:
    if (expected_read_term != VC_EVENT_EOS && expected_read_term != VC_EVENT_NONE) {
      record_error("read: Unexpected EOS Event");
    } else {
      read_finished();
    }
    break;
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_ERROR:
    if (expected_read_term != event && expected_read_term != VC_EVENT_NONE) {
      record_error("read: Unexpected error or timeout");
    } else {
      read_finished();
    }
    break;
  default:
    record_error("read: Unknown event");
    break;
  }
}

int
NetVCTest::main_handler(int event, void *data)
{
  if (event == NET_EVENT_ACCEPT) {
    test_vc = static_cast<NetVConnection *>(data);
    start_test();
    return 0;
  }

  if (data == read_vio) {
    read_handler(event);
  } else if (data == write_vio) {
    write_handler(event);
  } else {
    record_error("main: unknown event");
  }

  return 0;
}

NetTestDriver::NetTestDriver() : Continuation(nullptr) {}

NetTestDriver::~NetTestDriver() {}
