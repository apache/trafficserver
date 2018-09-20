/** @file

  Unit test for ArgParser

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

#include "catch.hpp"
#include "tscore/ArgParser.h"

int global;
ts::ArgParser parser;
ts::ArgParser parser2;

TEST_CASE("Parsing test", "[parse]")
{
  // initialize and construct the parser
  parser.add_global_usage("traffic_blabla [--SWITCH]");

  setenv("ENV_TEST", "env_test", 0);
  setenv("ENV_TEST2", "env_test2", 0);
  parser.add_option("--globalx", "-x", "global switch x", "ENV_TEST", 2, "", "globalx_key");
  parser.add_option("--globaly", "-y", "global switch y", "", 2, "default1 default2");
  parser.add_option("--globalz", "-z", "global switch z", "", MORE_THAN_ONE_ARG_N);

  ts::ArgParser::Command &init_command   = parser.add_command("init", "initialize traffic blabla", "ENV_TEST2", 1, nullptr);
  ts::ArgParser::Command &remove_command = parser.add_command("remove", "remove traffic blabla");

  init_command.add_option("--initoption", "-i", "init option");
  init_command.add_option("--initoption2", "-j", "init2 option", "", 1, "");
  init_command.add_command("subinit", "sub initialize traffic blabla", "", 2, nullptr, "subinit_key")
    .add_option("--subinitopt", "-s", "sub init option");

  remove_command.add_command("subremove", "sub remove traffic blabla").add_command("subsubremove", "sub sub remove");

  ts::Arguments parsed_data;

  // first run of arguments
  const char *argv1[] = {"traffic_blabla", "init", "a", "--initoption", "--globalx", "x", "y", NULL};
  parsed_data         = parser.parse(argv1);

  REQUIRE(parsed_data.get("init") == true);
  REQUIRE(parsed_data.get("init").env() == "env_test2");
  REQUIRE(parsed_data.get("globalx_key") == true);
  REQUIRE(parsed_data.get("globalx_key").env() == "env_test");
  REQUIRE(parsed_data.get("globaly") == true);
  REQUIRE(parsed_data.get("globaly").size() == 2);
  REQUIRE(parsed_data.get("globaly").value() == "default1");
  REQUIRE(parsed_data.get("globaly").at(1) == "default2");
  REQUIRE(parsed_data.get("initoption") == true);
  REQUIRE(parsed_data.get("a") == false);
  REQUIRE(parsed_data.get("init").env().size() != 0);
  REQUIRE(parsed_data.get("init").size() == 1);
  REQUIRE(parsed_data.get("init").at(0) == "a");
  REQUIRE(parsed_data.get("globalx_key").size() == 2);
  REQUIRE(parsed_data.get("globalx_key").value() == "x");
  REQUIRE(parsed_data.get("globalx_key")[1] == "y");

  // second run of arguments
  const char *argv2[] = {"traffic_blabla",    "init",         "i",  "subinit", "a",  "b",
                         "--initoption2=abc", "--subinitopt", "-y", "y1",      "y2", NULL};

  parsed_data = parser.parse(argv2);
  REQUIRE(parsed_data.get("init") == true);
  REQUIRE(parsed_data.get("subinitopt") == true);
  REQUIRE(parsed_data.get("globaly") == true);
  REQUIRE(parsed_data.get("globaly").size() == 2);
  REQUIRE(parsed_data.get("globaly")[0] == "y1");
  REQUIRE(parsed_data.get("globaly")[1] == "y2");
  REQUIRE(parsed_data.get("subinit_key").size() == 2);
  REQUIRE(parsed_data.get("subinit").size() == false);
  REQUIRE(parsed_data.get("initoption2").size() == 1);
  REQUIRE(parsed_data.get("initoption2")[0] == "abc");

  // third run of arguments
  const char *argv3[] = {"traffic_blabla", "-x",           "abc",          "xyz",          "remove", "subremove",
                         "subsubremove",   "--globalz=z1", "--globalz=z2", "--globalz=z3", NULL};

  parsed_data = parser.parse(argv3);
  REQUIRE(parsed_data.has_action() == false);
  REQUIRE(parsed_data.get("remove") == true);
  REQUIRE(parsed_data.get("subremove") == true);
  REQUIRE(parsed_data.get("subsubremove") == true);
  REQUIRE(parsed_data.get("globalx_key").size() == 2);
  REQUIRE(parsed_data.get("globalz").size() == 3);
}

void
test_method_1()
{
  global = 0;
  parser2.set_error("error");
  return;
}

void
test_method_2(int num)
{
  if (num == 1) {
    global = 1;
  } else {
    global = 2;
  }
}

TEST_CASE("Invoke test", "[invoke]")
{
  int num = 1;

  parser2.add_global_usage("traffic_blabla [--SWITCH]");
  // function by reference
  parser2.add_command("func", "some test function 1", "", 0, &test_method_1);
  // lambda
  parser2.add_command("func2", "some test function 2", "", 0, [&]() { return test_method_2(num); });

  ts::Arguments parsed_data;

  const char *argv1[] = {"traffic_blabla", "func", NULL};

  parsed_data = parser2.parse(argv1);
  REQUIRE(parsed_data.has_action() == true);
  parsed_data.invoke();
  REQUIRE(global == 0);
  REQUIRE(parser2.get_error() == "error");

  const char *argv2[] = {"traffic_blabla", "func2", NULL};

  parsed_data = parser2.parse(argv2);
  parsed_data.invoke();
  REQUIRE(global == 1);
  num = 3;
  parsed_data.invoke();
  REQUIRE(global == 2);
}
