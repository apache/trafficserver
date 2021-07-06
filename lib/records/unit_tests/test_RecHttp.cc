/** @file

   Catch-based tests for HdrsUtils.cc

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <string>
#include <string_view>
#include <array>

#include "catch.hpp"

#include "tscore/BufferWriter.h"
#include "records/I_RecHttp.h"
#include "test_Diags.h"

using ts::TextView;

TEST_CASE("RecHttp", "[librecords][RecHttp]")
{
  std::vector<HttpProxyPort> ports;
  CatchDiags *cdiag = static_cast<CatchDiags *>(diags);
  cdiag->messages.clear();

  SECTION("base")
  {
    HttpProxyPort::loadValue(ports, "8080");
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 8080);
  }

  SECTION("two")
  {
    HttpProxyPort::loadValue(ports, "8080 8090");
    REQUIRE(ports.size() == 2);
    REQUIRE(ports[0].m_port == 8080);
    REQUIRE(ports[1].m_port == 8090);
  }

  SECTION("family")
  {
    HttpProxyPort::loadValue(ports, "7070:ipv4:ip-in=192.168.56.1");
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 7070);
    REQUIRE(ports[0].m_family == AF_INET);
    REQUIRE(ports[0].isSSL() == false);
  }

  SECTION("crossed-family")
  {
    HttpProxyPort::loadValue(ports, "7070:ipv6:ip-in=192.168.56.1");
    REQUIRE(ports.size() == 0);
    REQUIRE(cdiag->messages.size() == 2);
    REQUIRE(cdiag->messages[0].find("[ipv6]") != std::string::npos);
    REQUIRE(cdiag->messages[0].find("[ipv4]") != std::string::npos);
  }

  SECTION("ipv6-a")
  {
    TextView descriptor{"4443:ssl:ip-in=[ffee::24c3:3349:3cee:0143]"};
    HttpProxyPort::loadValue(ports, descriptor.data());
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 4443);
    REQUIRE(ports[0].m_family == AF_INET6);
    REQUIRE(ports[0].isSSL() == true);
  }

  SECTION("dual-addr")
  {
    TextView descriptor{"4443:ssl:ipv6:ip-out=[ffee::24c3:3349:3cee:0143]:ip-out=10.1.2.3"};
    HttpProxyPort::loadValue(ports, descriptor.data());
    char buff[256];
    ports[0].print(buff, sizeof(buff));
    std::string_view view{buff};
    REQUIRE(ports.size() == 1);
    REQUIRE(ports[0].m_port == 4443);
    REQUIRE(ports[0].m_family == AF_INET6);
    REQUIRE(ports[0].isSSL() == true);
    REQUIRE(ports[0].m_outbound_ip6.isValid() == true);
    REQUIRE(ports[0].m_outbound_ip4.isValid() == true);
    REQUIRE(ports[0].m_inbound_ip.isValid() == false);
    REQUIRE(view.find(":ssl") != TextView::npos);
    REQUIRE(view.find(":proto") == TextView::npos); // it's default, should not have this.
  }
}
