/** @file

  Unit tests for time-based log rolling.

  LogObject::roll_files() takes the current time as an argument, so these tests
  drive it across a synthetic midnight rather than waiting out a real rolling
  interval. The AuTest harness turns rolling off (a test running across 00:00
  must not have its logs renamed under it), so this is where automatic rolling
  itself is covered.

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

#include <catch2/catch_test_macros.hpp>

#include "iocore/utils/Machine.h"
#include "proxy/logging/Log.h"
#include "proxy/logging/LogConfig.h"
#include "proxy/logging/LogFile.h"
#include "proxy/logging/LogFormat.h"
#include "proxy/logging/LogObject.h"

#include <unistd.h>

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
constexpr int ONE_DAY = 86400;

/// Local time @a days_ahead days from today at @a hour : @a min : @a sec.
/// seconds_to_next_roll() works in local time, so the boundaries are built the same way.
time_t
local_time(int days_ahead, int hour, int min, int sec)
{
  time_t    now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  lt.tm_mday  += days_ahead;
  lt.tm_hour   = hour;
  lt.tm_min    = min;
  lt.tm_sec    = sec;
  lt.tm_isdst  = -1;
  return mktime(&lt);
}

/// The roll-name timestamp BaseLogFile appends for @a t.
std::string
roll_stamp(time_t t)
{
  struct tm lt;
  char      buf[64];
  localtime_r(&t, &lt);
  strftime(buf, sizeof(buf), "%Y%m%d.%Hh%Mm%Ss", &lt);
  return buf;
}

std::string
slurp(const fs::path &path)
{
  std::ifstream in(path);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<fs::path>
rolled_files(const fs::path &dir)
{
  std::vector<fs::path> out;
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".old") {
      out.push_back(entry.path());
    }
  }
  return out;
}

/// A fresh directory per test case, and the process-wide logging state the
/// LogObject and LogFile code reads.
struct RollingFixture {
  fs::path                   dir;
  std::unique_ptr<LogConfig> cfg; // built after Machine::init, which its constructor needs

  explicit RollingFixture(const char *name)
  {
    static bool initialized = false;
    if (!initialized) {
      Machine::init("localhost", nullptr);
      Log::init_fields();
      LogConfig::register_stat_callbacks(); // opening and rolling a log file bumps log metrics
      initialized = true;
    }
    cfg         = std::make_unique<LogConfig>();
    Log::config = cfg.get();
    dir         = fs::temp_directory_path() / (std::string("test_LogRolling.") + name + "." + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir);
  }

  ~RollingFixture()
  {
    Log::config = nullptr;
    fs::remove_all(dir);
  }
};

/// An ASCII log object that rolls daily at local midnight, as the defaults do,
/// and reopens its file after each roll.
LogObject *
make_daily_object(RollingFixture &fx, Log::RollingEnabledValues rolling_enabled)
{
  LogFormat fmt("rolltest", "%<cqu>");
  REQUIRE(fmt.valid());
  return new LogObject(fx.cfg.get(), &fmt, fx.dir.c_str(), "roll_test", LOG_FILE_ASCII, nullptr, rolling_enabled, 1, ONE_DAY,
                       /* rolling_offset_hr */ 0, /* rolling_size_mb */ 0, /* auto_created */ false, /* rolling_max_count */ 0,
                       /* rolling_min_count */ 0, /* reopen_after_rolling */ true, /* pipe_buffer_size */ 0, /* fast */ true);
}

void
append(const fs::path &path, const std::string &line)
{
  std::ofstream out(path, std::ios::app);
  out << line << '\n';
}
} // namespace

TEST_CASE("roll_files rolls a daily log at local midnight, and only once", "[logging][rolling]")
{
  RollingFixture fx("daily");
  LogObject     *obj = make_daily_object(fx, Log::ROLL_ON_TIME_ONLY);
  fs::path       log = fx.dir / "roll_test.log";

  REQUIRE(fs::exists(log)); // reopen_after_rolling opens it up front
  append(log, "before midnight");

  // Tomorrow night, so the object's own "last rolled" time (now) is far behind.
  time_t before  = local_time(1, 23, 59, 59);
  time_t after   = local_time(2, 0, 0, 1);
  time_t shortly = local_time(2, 0, 0, 5);

  CHECK(obj->roll_files(before) == 0);
  CHECK(rolled_files(fx.dir).empty());

  REQUIRE(obj->roll_files(after) == 1);
  auto rolled = rolled_files(fx.dir);
  REQUIRE(rolled.size() == 1);
  CHECK(rolled[0].filename().string().find(roll_stamp(after) + ".old") != std::string::npos);
  CHECK(slurp(rolled[0]) == "before midnight\n");

  // The original name is a new, empty file, ready for the next record.
  REQUIRE(fs::exists(log));
  CHECK(fs::file_size(log) == 0);

  // A second periodic check inside the same boundary window must not roll again.
  CHECK(obj->roll_files(shortly) == 0);
  CHECK(rolled_files(fx.dir).size() == 1);

  // The next midnight rolls again.
  append(log, "the next day");
  CHECK(obj->roll_files(after + ONE_DAY) == 1);
  CHECK(rolled_files(fx.dir).size() == 2);

  delete obj;
}

TEST_CASE("rolling_enabled 0 never rolls, even at midnight", "[logging][rolling]")
{
  RollingFixture fx("disabled");
  LogObject     *obj = make_daily_object(fx, Log::NO_ROLLING);
  fs::path       log = fx.dir / "roll_test.log";

  append(log, "kept");
  CHECK(obj->roll_files(local_time(2, 0, 0, 1)) == 0);
  CHECK(rolled_files(fx.dir).empty());
  CHECK(slurp(log) == "kept\n");

  delete obj;
}

TEST_CASE("a record written after a roll lands in the new file", "[logging][rolling]")
{
  RollingFixture fx("reopen");
  fs::path       log = fx.dir / "roll_test.log";
  Ptr<LogFile>   file(new LogFile(log.c_str(), nullptr, LOG_FILE_ASCII, 0));

  REQUIRE(file->open_file() == LogFile::LOG_FILE_NO_ERROR);

  char before[] = "before the roll\n";
  REQUIRE(LogFile::writeln(before, sizeof(before) - 1, file->get_fd(), log.c_str()) > 0);

  time_t start = local_time(1, 0, 0, 0);
  REQUIRE(file->roll(start, start + ONE_DAY, /* reopen_after_rolling */ true) == 1);

  char after[] = "after the roll\n";
  REQUIRE(LogFile::writeln(after, sizeof(after) - 1, file->get_fd(), log.c_str()) > 0);

  auto rolled = rolled_files(fx.dir);
  REQUIRE(rolled.size() == 1);
  CHECK(slurp(rolled[0]) == "before the roll\n");
  CHECK(slurp(log) == "after the roll\n");
}
