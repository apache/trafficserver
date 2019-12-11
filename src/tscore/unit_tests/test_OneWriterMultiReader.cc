/** @file

    Tests for classes defined in OneWriterMultiReader.h

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

#include "OneWriterMultiReader.h"

#include <catch.hpp>

#include <thread>
#include <atomic>

namespace
{
const int Data_array_dim = 20;

int data[Data_array_dim];

const int Last_data_value = 1000;

std::atomic<bool> fail;

void
failure()
{
  std::exit(1);
  fail = true;
}

template <class RWLock, RWLock &Rwl> class ReaderThreads
{
private:
  static void
  tfunc()
  {
    int previous = 0;

    do {
      {
        typename RWLock::ReadLock rl(Rwl);

        if (!rl.is_locked()) {
          failure();
          return;
        }

        if (data[0] == previous) {
          continue;
        }

        for (int i = 1; i < Data_array_dim; ++i) {
          if (data[i] != data[0]) {
            failure();
            return;
          }
        }

        if (data[0] < previous) {
          failure();
          return;
        }
        previous = data[0];
      }
      std::this_thread::yield();

    } while (previous < Last_data_value);

    if (previous > Last_data_value) {
      failure();
    }
  }

  static const int Num = 100;

  std::thread thr[Num];

public:
  void
  startAll()
  {
    for (int i = 0; i < Num; ++i) {
      thr[i] = std::thread(tfunc);
    }
  }

  void
  joinAll()
  {
    for (int i = 0; i < Num; ++i) {
      thr[i].join();
    }
  }
};

template <class RWLock, RWLock &Rwl>
void
write_tfunc()
{
  int previous = 0;

  for (;;) {
    {
      typename RWLock::WriteLock wl(Rwl);

      if (!wl.is_locked()) {
        failure();
        return;
      }

      if (data[0] < previous) {
        failure();
        return;
      }

      if (data[0] >= Last_data_value) {
        break;
      }

      ++data[0];
      for (int i = 1; i < Data_array_dim; ++i) {
        std::this_thread::yield();
        if (data[i] < previous) {
          failure();
          return;
        }
        data[i] = data[0];
      }

      previous = data[0];
    }
    std::this_thread::yield();
  }
  for (int i = 0; i < Data_array_dim; ++i) {
    if (data[i] != Last_data_value) {
      failure();
      return;
    }
  }
}

ts::OneWriterMultiReader owmr;

ts::ExclusiveWriterMultiReader ewmr;

} // end anonymous namespace

TEST_CASE("OneWriterMultiReader", "[libts][OWMR]")
{
  fail = false;

  for (int i = 0; i < Data_array_dim; ++i) {
    data[i] = 0;
  }

  ReaderThreads<ts::OneWriterMultiReader, owmr> owmr_rths;

  owmr_rths.startAll();

  REQUIRE(!fail);

  std::thread owth(write_tfunc<ts::OneWriterMultiReader, owmr>);

  owmr_rths.joinAll();

  owth.join();

  REQUIRE(!fail);

  for (int i = 0; i < Data_array_dim; ++i) {
    data[i] = 0;
  }

  ReaderThreads<ts::ExclusiveWriterMultiReader, ewmr> ewmr_rths;

  ewmr_rths.startAll();

  REQUIRE(!fail);

  const int Num_writer_threads = 100;

  std::thread ewth[Num_writer_threads];

  for (int i = 0; i < Num_writer_threads; ++i) {
    ewth[i] = std::thread(write_tfunc<ts::ExclusiveWriterMultiReader, ewmr>);
  }

  ewmr_rths.joinAll();

  for (int i = 0; i < Num_writer_threads; ++i) {
    ewth[i].join();
  }

  REQUIRE(!fail);

  // Test lock deferral, read lock try_lock().
  {
    ts::OneWriterMultiReader::WriteLock wl(owmr, std::defer_lock);

    REQUIRE(!wl.is_locked());

    ts::OneWriterMultiReader::ReadLock rl(owmr, std::defer_lock);

    REQUIRE(!rl.is_locked());

    wl.lock();

    REQUIRE(wl.is_locked());

    REQUIRE(!rl.try_lock());

    wl.unlock();

    REQUIRE(!wl.is_locked());

    REQUIRE(rl.try_lock());
  }
  {
    ts::ExclusiveWriterMultiReader::WriteLock wl(ewmr, std::defer_lock);

    REQUIRE(!wl.is_locked());

    wl.lock();

    REQUIRE(wl.is_locked());

    wl.unlock();

    REQUIRE(!wl.is_locked());
  }
}
