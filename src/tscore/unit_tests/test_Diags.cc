/** @file

    Tests for stuff defined in Diags.h  Currently only tests
    DiagsTagHelper.

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

#include <catch.hpp>

#include <thread>
#include <atomic>

#include <tscore/Diags.h>

class DiagsUnitTest : private ts::detail::DiagsTagHelper
{
public:
  struct DTH : private ts::detail::DiagsTagHelper {
    using ts::detail::DiagsTagHelper::flag_for_tag;
    using ts::detail::DiagsTagHelper::activate_taglist;
  };
};

namespace
{
DiagsUnitTest::DTH dth;

std::atomic<bool> fail{false};

std::atomic<unsigned> global_step{0};

void
failure()
{
  fail = true;
}

class LookupThreads
{
private:
  static void
  tfunc(int tag_major, int tag_minor)
  {
    DiagEnabled const *de = nullptr;

    int step      = 0;
    int last_step = 2 == tag_major ? 2 : 4;

    char tag[4];
    tag[0] = '0' + tag_major;
    tag[1] = '.';
    tag[2] = '0' + tag_minor;
    tag[3] = '\0';

    do {
      DiagEnabled const *de2 = dth.flag_for_tag(tag);

      if (!de2) {
        failure();
        break;
      }
      if (de) {
        if (de2 != de) {
          failure();
          break;
        }
      } else {
        de = de2;
      }

      if (((step & 1) != 0) != (*de != 0)) {
        ++step;
        ++global_step;

      } else {
        std::this_thread::yield();
      }
      if (step > last_step) {
        failure();
        break;
      }
    } while (!done);
    if (step != last_step) {
      failure();
    }
  }

  static std::thread thr[20];

  static std::atomic<bool> done;

public:
  static void
  startAll()
  {
    int i = 0;
    for (int tag_major = 1; tag_major <= 2; ++tag_major) {
      for (int tag_minor = 0; tag_minor <= 9; ++tag_minor) {
        thr[i++] = std::thread(tfunc, tag_major, tag_minor);
      }
    }
  }

  static void
  joinAll()
  {
    done = true;

    int i = 0;
    for (int tag_major = 1; tag_major <= 2; ++tag_major) {
      for (int tag_minor = 0; tag_minor <= 9; ++tag_minor) {
        thr[i++].join();
      }
    }
  }
};

std::thread LookupThreads::thr[20];

std::atomic<bool> LookupThreads::done{false};

} // end anonymous namespace

TEST_CASE("Diags", "[libts][diags]")
{
  dth.activate_taglist(nullptr);

  LookupThreads::startAll();

  dth.activate_taglist("1");

  while (global_step != 10) {
    std::this_thread::yield();
  }

  dth.activate_taglist("2");

  while (global_step != 30) {
    std::this_thread::yield();
  }

  dth.activate_taglist("1|2");

  while (global_step != 40) {
    std::this_thread::yield();
  }

  dth.activate_taglist(nullptr);

  while (global_step != 60) {
    std::this_thread::yield();
  }

  LookupThreads::joinAll();

  REQUIRE(!fail);
}
