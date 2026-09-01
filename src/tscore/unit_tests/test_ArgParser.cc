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

#include <catch2/catch_test_macros.hpp>
#include "tscore/ArgParser.h"

int           global;
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
  const char *argv1[] = {"traffic_blabla", "init", "a", "--initoption", "--globalx", "x", "y", nullptr};
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
                         "--initoption2=abc", "--subinitopt", "-y", "y1",      "y2", nullptr};

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
                         "subsubremove",   "--globalz=z1", "--globalz=z2", "--globalz=z3", nullptr};

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

  const char *argv1[] = {"traffic_blabla", "func", nullptr};

  parsed_data = parser2.parse(argv1);
  REQUIRE(parsed_data.has_action() == true);
  parsed_data.invoke();
  REQUIRE(global == 0);
  REQUIRE(parser2.get_error() == "error");

  const char *argv2[] = {"traffic_blabla", "func2", nullptr};

  parsed_data = parser2.parse(argv2);
  parsed_data.invoke();
  REQUIRE(global == 1);
  num = 3;
  parsed_data.invoke();
  REQUIRE(global == 2);
}

TEST_CASE("Case sensitive short options", "[parse]")
{
  ts::ArgParser cs_parser;
  cs_parser.add_global_usage("test_prog [--SWITCH]");

  // Add a command with two options that differ only in case: -t and -T
  ts::ArgParser::Command &cmd = cs_parser.add_command("process", "process data");
  cmd.add_option("--tag", "-t", "a label", "", 1, "");
  cmd.add_option("--threshold", "-T", "a numeric value", "", 1, "100");

  ts::Arguments parsed;

  // Use lowercase -t: should set "tag" only
  const char *argv1[] = {"test_prog", "process", "-t", "my_tag", nullptr};
  parsed              = cs_parser.parse(argv1);
  REQUIRE(parsed.get("tag") == true);
  REQUIRE(parsed.get("tag").value() == "my_tag");
  // threshold should still have its default
  REQUIRE(parsed.get("threshold").value() == "100");

  // Use uppercase -T: should set "threshold" only
  const char *argv2[] = {"test_prog", "process", "-T", "200", nullptr};
  parsed              = cs_parser.parse(argv2);
  REQUIRE(parsed.get("threshold") == true);
  REQUIRE(parsed.get("threshold").value() == "200");
  // tag should be empty (no default)
  REQUIRE(parsed.get("tag").value() == "");

  // Use both -t and -T together
  const char *argv3[] = {"test_prog", "process", "-t", "foo", "-T", "500", nullptr};
  parsed              = cs_parser.parse(argv3);
  REQUIRE(parsed.get("tag") == true);
  REQUIRE(parsed.get("tag").value() == "foo");
  REQUIRE(parsed.get("threshold") == true);
  REQUIRE(parsed.get("threshold").value() == "500");
}

TEST_CASE("with_required does not trigger on default values", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  ts::ArgParser::Command &cmd = parser.add_command("scan", "scan targets");
  cmd.add_option("--tag", "-t", "a label", "", 1, "");
  cmd.add_option("--verbose", "-v", "enable verbose output");
  cmd.add_option("--threshold", "-T", "a numeric value", "", 1, "100").with_required("--verbose");

  // -t alone should NOT trigger --threshold's dependency on --verbose.
  // The default value "100" for --threshold must not count as "explicitly used".
  const char   *argv1[] = {"test_prog", "scan", "-t", "my_tag", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("tag").value() == "my_tag");
  // threshold default should still be applied after validation
  REQUIRE(parsed.get("threshold").value() == "100");

  // -T with -v should work fine
  const char *argv2[] = {"test_prog", "scan", "-T", "200", "-v", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("threshold").value() == "200");
  REQUIRE(parsed.get("verbose") == true);

  // -t and -T together with -v should work
  const char *argv3[] = {"test_prog", "scan", "-t", "foo", "-T", "300", "-v", nullptr};
  parsed              = parser.parse(argv3);
  REQUIRE(parsed.get("tag").value() == "foo");
  REQUIRE(parsed.get("threshold").value() == "300");
  REQUIRE(parsed.get("verbose") == true);
}

TEST_CASE("Variable argument option stops at a following option", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  ts::ArgParser::Command &cmd = parser.add_command("reload", "reload configs");
  cmd.add_option("--directive", "-D", "reload directives", "", MORE_THAN_ZERO_ARG_N, "");
  cmd.add_option("--token", "-t", "a token", "", 1, "");
  cmd.add_option("--monitor", "-m", "monitor progress");

  // A flag after a variable argument option is not swallowed as a value.
  const char   *argv1[] = {"test_prog", "reload", "-D", "a.id=1", "b.id=2", "-m", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("directive").size() == 2);
  REQUIRE(parsed.get("directive")[0] == "a.id=1");
  REQUIRE(parsed.get("directive")[1] == "b.id=2");
  REQUIRE(parsed.get("monitor") == true);

  // A following option keeps its own argument.
  const char *argv2[] = {"test_prog", "reload", "-D", "a.id=1", "-t", "my_token", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("directive").size() == 1);
  REQUIRE(parsed.get("directive")[0] == "a.id=1");
  REQUIRE(parsed.get("token").value() == "my_token");

  // The long form of the following option is recognized too.
  const char *argv3[] = {"test_prog", "reload", "-D", "a.id=1", "--monitor", nullptr};
  parsed              = parser.parse(argv3);
  REQUIRE(parsed.get("directive").size() == 1);
  REQUIRE(parsed.get("monitor") == true);

  // So is its --option=value form.
  const char *argv4[] = {"test_prog", "reload", "-D", "a.id=1", "--token=my_token", nullptr};
  parsed              = parser.parse(argv4);
  REQUIRE(parsed.get("directive").size() == 1);
  REQUIRE(parsed.get("token").value() == "my_token");
}

TEST_CASE("Double dash ends option recognition for variable argument options", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  ts::ArgParser::Command &cmd = parser.add_command("reload", "reload configs");
  cmd.add_option("--directive", "-D", "reload directives", "", MORE_THAN_ZERO_ARG_N, "");
  cmd.add_option("--monitor", "-m", "monitor progress");

  // After "--" a token that looks like an option is taken as a value instead.
  const char   *argv[] = {"test_prog", "reload", "-D", "--", "-m", "a.id=1", nullptr};
  ts::Arguments parsed = parser.parse(argv);
  REQUIRE(parsed.get("directive").size() == 2);
  REQUIRE(parsed.get("directive")[0] == "-m");
  REQUIRE(parsed.get("directive")[1] == "a.id=1");
  REQUIRE(parsed.get("monitor") == false);
}

TEST_CASE("Option value keeps embedded equal signs", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  ts::ArgParser::Command &cmd = parser.add_command("reload", "reload configs");
  cmd.add_option("--directive", "-D", "reload directives", "", MORE_THAN_ZERO_ARG_N, "");

  // Only the first '=' separates the option from its value.
  const char   *argv[] = {"test_prog", "reload", "--directive=ip_allow.id=foo", nullptr};
  ts::Arguments parsed = parser.parse(argv);
  REQUIRE(parsed.get("directive").size() == 1);
  REQUIRE(parsed.get("directive")[0] == "ip_allow.id=foo");
}

TEST_CASE("An option taking at most one argument leaves the positional arguments alone", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  // Mirrors "traffic_ctl config get [--cold [FILE]] RECORD [RECORD ...]".
  ts::ArgParser::Command &cmd = parser.add_command("get", "get values", "", MORE_THAN_ONE_ARG_N, nullptr);
  cmd.add_option("--cold", "-c", "read from a file", "", AT_MOST_ONE_ARG_N);
  cmd.add_option("--records", "", "yaml output");

  // The option takes its single value and stops, so the command keeps its own arguments.
  const char   *argv1[] = {"test_prog", "get", "-c", "records.yaml", "proxy.config.x", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("cold").value() == "records.yaml");
  REQUIRE(parsed.get("get").size() == 1);
  REQUIRE(parsed.get("get")[0] == "proxy.config.x");

  // Several positional arguments are unaffected.
  const char *argv2[] = {"test_prog", "get", "-c", "records.yaml", "proxy.config.x", "proxy.config.y", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("cold").value() == "records.yaml");
  REQUIRE(parsed.get("get").size() == 2);
  REQUIRE(parsed.get("get")[1] == "proxy.config.y");

  // Trailing placement keeps working.
  const char *argv3[] = {"test_prog", "get", "proxy.config.x", "-c", "records.yaml", nullptr};
  parsed              = parser.parse(argv3);
  REQUIRE(parsed.get("cold").value() == "records.yaml");
  REQUIRE(parsed.get("get").size() == 1);

  // The --option=value form is not mistaken for a fixed arity mismatch.
  const char *argv4[] = {"test_prog", "get", "--cold=records.yaml", "proxy.config.x", nullptr};
  parsed              = parser.parse(argv4);
  REQUIRE(parsed.get("cold").value() == "records.yaml");
  REQUIRE(parsed.get("get").size() == 1);
}

TEST_CASE("An option taking at most one argument accepts no value at all", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  ts::ArgParser::Command &cmd = parser.add_command("get", "get values", "", MORE_THAN_ONE_ARG_N, nullptr);
  cmd.add_option("--cold", "-c", "read from a file", "", AT_MOST_ONE_ARG_N);
  cmd.add_option("--records", "", "yaml output");

  // Called with no value, so the caller falls back to its own default.
  const char   *argv1[] = {"test_prog", "get", "proxy.config.x", "-c", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("cold") == true);
  REQUIRE(parsed.get("cold").size() == 0);
  REQUIRE(parsed.get("cold").value().empty());
  REQUIRE(parsed.get("get").size() == 1);

  // A following option is never taken as the value.
  const char *argv2[] = {"test_prog", "get", "-c", "--records", "proxy.config.x", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("cold").size() == 0);
  REQUIRE(parsed.get("records") == true);
  REQUIRE(parsed.get("get").size() == 1);
  REQUIRE(parsed.get("get")[0] == "proxy.config.x");

  // After "--" even a token shaped like an option becomes the value.
  const char *argv3[] = {"test_prog", "get", "-c", "--", "-weird-name.yaml", "proxy.config.x", nullptr};
  parsed              = parser.parse(argv3);
  REQUIRE(parsed.get("cold").value() == "-weird-name.yaml");
  REQUIRE(parsed.get("get").size() == 1);
  REQUIRE(parsed.get("get")[0] == "proxy.config.x");
}

TEST_CASE("An option taking a fixed number of arguments can be given a value shaped like an option", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  // Mirrors "traffic_ctl server debug enable [--tags TAGS] [--append]".
  ts::ArgParser::Command &cmd = parser.add_command("enable", "enable debug");
  cmd.add_option("--tags", "-t", "debug tags", "", 1);
  cmd.add_option("--append", "-a", "append to the existing tags");

  // A value that starts with '-' is passed after "--", which is otherwise taken as naming an
  // option and reported as a missing value.
  const char   *argv1[] = {"test_prog", "enable", "-t", "--", "-a", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("tags").value() == "-a");
  REQUIRE(parsed.get("append") == false);

  // The --option=value form needs no escape.
  const char *argv2[] = {"test_prog", "enable", "--tags=-a", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("tags").value() == "-a");
  REQUIRE(parsed.get("append") == false);

  // An option written after the value keeps its own meaning.
  const char *argv3[] = {"test_prog", "enable", "-t", "http", "-a", nullptr};
  parsed              = parser.parse(argv3);
  REQUIRE(parsed.get("tags").value() == "http");
  REQUIRE(parsed.get("append") == true);
}

TEST_CASE("A repeated variable argument option accumulates its values", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  // Mirrors "traffic_ctl config reload [-D DIRECTIVE...] [-d SOURCE...] [-m]".
  ts::ArgParser::Command &cmd = parser.add_command("reload", "reload configs");
  cmd.add_option("--directive", "-D", "reload directives", "", MORE_THAN_ZERO_ARG_N, "");
  cmd.add_option("--data", "-d", "inline config data", "", MORE_THAN_ZERO_ARG_N, "");
  cmd.add_option("--monitor", "-m", "monitor progress");

  // Collection stops at the second -D, so the values of the first must survive it.
  const char   *argv1[] = {"test_prog", "reload", "-D", "a.id=1", "-D", "b.id=2", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("directive").size() == 2);
  REQUIRE(parsed.get("directive")[0] == "a.id=1");
  REQUIRE(parsed.get("directive")[1] == "b.id=2");

  // Each occurrence keeps every value it collected, in the order written.
  const char *argv2[] = {"test_prog", "reload", "-D", "a.id=1", "b.id=2", "-D", "c.id=3", "d.id=4", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("directive").size() == 4);
  REQUIRE(parsed.get("directive")[0] == "a.id=1");
  REQUIRE(parsed.get("directive")[3] == "d.id=4");

  // An unrelated option written between the two occurrences keeps its own meaning.
  const char *argv3[] = {"test_prog", "reload", "-D", "a.id=1", "-m", "-D", "b.id=2", nullptr};
  parsed              = parser.parse(argv3);
  REQUIRE(parsed.get("directive").size() == 2);
  REQUIRE(parsed.get("directive")[1] == "b.id=2");
  REQUIRE(parsed.get("monitor") == true);

  // The two spellings count against the same option, in either order.
  const char *argv4[] = {"test_prog", "reload", "-D", "a.id=1", "--directive=b.id=2", nullptr};
  parsed              = parser.parse(argv4);
  REQUIRE(parsed.get("directive").size() == 2);
  REQUIRE(parsed.get("directive")[0] == "a.id=1");
  REQUIRE(parsed.get("directive")[1] == "b.id=2");

  const char *argv5[] = {"test_prog", "reload", "--directive=a.id=1", "--directive=b.id=2", nullptr};
  parsed              = parser.parse(argv5);
  REQUIRE(parsed.get("directive").size() == 2);

  // Repeated -d merges the same way, which is what the documented multi-source reload needs.
  const char *argv6[] = {"test_prog", "reload", "-d", "@ip_allow.yaml", "-d", "@sni.yaml", nullptr};
  parsed              = parser.parse(argv6);
  REQUIRE(parsed.get("data").size() == 2);
  REQUIRE(parsed.get("data")[0] == "@ip_allow.yaml");
  REQUIRE(parsed.get("data")[1] == "@sni.yaml");

  // Two different options each keep their own values.
  const char *argv7[] = {"test_prog", "reload", "-D", "a.id=1", "-d", "@f.yaml", "-D", "b.id=2", nullptr};
  parsed              = parser.parse(argv7);
  REQUIRE(parsed.get("directive").size() == 2);
  REQUIRE(parsed.get("data").size() == 1);
  REQUIRE(parsed.get("data")[0] == "@f.yaml");
}

TEST_CASE("A repeated option requiring at least one argument accumulates too", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  // Mirrors "traffic_ctl rpc invoke [--params PARAM...]".
  ts::ArgParser::Command &cmd = parser.add_command("invoke", "invoke a method");
  cmd.add_option("--params", "-p", "request parameters", "", MORE_THAN_ONE_ARG_N, "");
  cmd.add_option("--format", "-f", "output format", "", 1, "");

  const char   *argv1[] = {"test_prog", "invoke", "-p", "one", "-p", "two", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("params").size() == 2);
  REQUIRE(parsed.get("params")[0] == "one");
  REQUIRE(parsed.get("params")[1] == "two");

  // The arity is satisfied by the first occurrence, so a later one is not left short.
  const char *argv2[] = {"test_prog", "invoke", "-p", "one", "-f", "json", "-p", "two", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("params").size() == 2);
  REQUIRE(parsed.get("format").value() == "json");
}

TEST_CASE("A default command does not repeat the values of a global option", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog CMD [OPTIONS]");

  // Mirrors traffic_layout, where "info" is the default command and --run-root is global. The
  // first pass matches no command and is retried with the default inserted, so a global option
  // is parsed twice and must not collect its value twice.
  parser.add_option("--run-root", "", "runroot", "", 1);
  parser.add_option("--tag", "", "tags", "", MORE_THAN_ZERO_ARG_N, "");
  parser.add_command("info", "show the layout").set_default();

  const char   *argv1[] = {"test_prog", "--run-root", "/tmp/rr", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("info") == true);
  REQUIRE(parsed.get("run-root").size() == 1);
  REQUIRE(parsed.get("run-root").value() == "/tmp/rr");

  // An accumulating option is the case that would double, since it is the one that keeps what
  // an earlier pass collected.
  const char *argv2[] = {"test_prog", "--tag", "a", "b", nullptr};
  parsed              = parser.parse(argv2);
  REQUIRE(parsed.get("info") == true);
  REQUIRE(parsed.get("tag").size() == 2);
  REQUIRE(parsed.get("tag")[0] == "a");
  REQUIRE(parsed.get("tag")[1] == "b");

  // Naming the command explicitly takes the same path once.
  const char *argv3[] = {"test_prog", "info", "--run-root", "/tmp/rr", nullptr};
  parsed              = parser.parse(argv3);
  REQUIRE(parsed.get("run-root").size() == 1);
}

TEST_CASE("A repeated option taking a fixed number of arguments keeps the last value", "[parse]")
{
  ts::ArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  // Mirrors "traffic_ctl server debug enable [--tags TAGS]". Only an unbounded arity
  // accumulates; a fixed one keeps the behaviour it has always had.
  ts::ArgParser::Command &cmd = parser.add_command("enable", "enable debug");
  cmd.add_option("--tags", "-t", "debug tags", "", 1);

  const char   *argv1[] = {"test_prog", "enable", "-t", "http", "-t", "cache", nullptr};
  ts::Arguments parsed  = parser.parse(argv1);
  REQUIRE(parsed.get("tags").size() == 1);
  REQUIRE(parsed.get("tags").value() == "cache");
}
