/** @file

  Test HostFile

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

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include <string>

#include "swoc/bwf_base.h"

#include "iocore/hostdb/HostFile.h"
#include "P_HostDBProcessor.h"

using namespace std::literals;

namespace
{

DbgCtl dbg_ctl_hostdb{"hostdb"};

const std::string_view hosts_data = "127.0.0.1 localhost\n::1 localhost\n1.2.3.4  host1\n4.3.2.1 host2 host3\n";

} // end anonymous namespace

void
spit(const swoc::file::path &p, std::string_view data)
{
  std::ofstream f(p.c_str(), std::ios::trunc);
  f.write(data.data(), data.size());
  f.close();
}

TEST_CASE("HostFile", "[hostdb]")
{
  auto                          tmp = swoc::file::temp_directory_path();
  swoc::LocalBufferWriter<1024> w;
  w.print("{}/localhost.{}", tmp, ::getpid());

  auto hostfilepath = swoc::file::path(w.view());

  spit(hostfilepath, hosts_data);

  auto hf = ParseHostFile(hostfilepath, ts_seconds(3600));

  REQUIRE(hf);

  SECTION("reverse lookup localhost v4")
  {
    HostDBHash h;
    h.ip.load("127.0.0.1");
    h.db_mark = HOSTDB_MARK_GENERIC;

    auto result = hf->lookup(h);

    REQUIRE(result);
    REQUIRE(result->name_view() == "localhost");
  }

  SECTION("reverse lookup localhost v6")
  {
    HostDBHash h;
    h.ip.load("::1");
    h.db_mark = HOSTDB_MARK_GENERIC;

    auto result = hf->lookup(h);

    REQUIRE(result);
    REQUIRE(result->name_view() == "localhost");
  }

  SECTION("reverse lookup host v4")
  {
    HostDBHash h;
    h.ip.load("4.3.2.1");
    h.db_mark = HOSTDB_MARK_GENERIC;

    auto result = hf->lookup(h);

    REQUIRE(result);
    REQUIRE(result->name_view() == "host2");
  }

  SECTION("forward lookup localhost v4")
  {
    HostDBHash h;
    h.host_name = "localhost"sv;
    h.db_mark   = HOSTDB_MARK_IPV4;

    auto result = hf->lookup(h);

    REQUIRE(result);
    REQUIRE(result->name_view() == h.host_name);
  }

  SECTION("forward lookup localhost v6")
  {
    HostDBHash h;
    h.host_name = "localhost"sv;
    h.db_mark   = HOSTDB_MARK_IPV6;

    auto result = hf->lookup(h);

    REQUIRE(result);
    REQUIRE(result->name_view() == h.host_name);
  }

  SECTION("forward lookup host v6")
  {
    HostDBHash h;
    h.host_name = "host1"sv;
    h.db_mark   = HOSTDB_MARK_IPV4;

    auto result = hf->lookup(h);

    REQUIRE(result);
    REQUIRE(result->name_view() == h.host_name);
  }
}
