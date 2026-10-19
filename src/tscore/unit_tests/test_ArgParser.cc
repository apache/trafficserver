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
#include <catch2/matchers/catch_matchers.hpp>
#include "tscore/ArgParser.h"

#include <iostream>
#include <sstream>
#include <string>

class TestArgParser : public ts::ArgParser
{
public:
  TestArgParser() { ts::ArgParser::set_test_mode(true); }
};

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

TEST_CASE("A declared environment variable fills a value the command line omitted", "[parse][env]")
{
  setenv("TS_ARGPARSER_TEST_FIXED", "from_env", 1);
  setenv("TS_ARGPARSER_TEST_ATMOST", "from_env", 1);
  setenv("TS_ARGPARSER_TEST_ATLEAST", "from_env", 1);
  unsetenv("TS_ARGPARSER_TEST_UNSET");

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  // Mirrors "traffic_ctl server debug enable [--tags TAGS]" with TS_DEBUG_TAGS.
  ts::ArgParser::Command &cmd = parser.add_command("enable", "enable debug");
  cmd.add_option("--tags", "-t", "debug tags", "TS_ARGPARSER_TEST_FIXED", 1);
  // Mirrors "traffic_ctl config get [--cold [FILE]]" with TS_RECORD_YAML.
  cmd.add_option("--cold", "-c", "read from a file", "TS_ARGPARSER_TEST_ATMOST", AT_MOST_ONE_ARG_N);
  cmd.add_option("--params", "-p", "parameters", "TS_ARGPARSER_TEST_ATLEAST", MORE_THAN_ONE_ARG_N);
  cmd.add_option("--undeclared", "-u", "no variable declared", "", 1);
  cmd.add_option("--declared-unset", "-U", "variable declared but unset", "TS_ARGPARSER_TEST_UNSET", 1);
  cmd.add_option("--monitor", "-m", "monitor progress");

  // Mirrors "traffic_ctl config get RECORD [RECORD ...]", whose positionals would absorb a
  // stray empty token and hide the bug an option without positionals cannot expose.
  ts::ArgParser::Command &get_cmd = parser.add_command("get", "get records", "", MORE_THAN_ONE_ARG_N, nullptr);
  get_cmd.add_option("--tags", "-t", "debug tags", "TS_ARGPARSER_TEST_FIXED", 1);

  // Fixed arity: the flag is typed with no value, so the variable supplies it instead of a
  // usage error.
  {
    const char   *argv[] = {"test_prog", "enable", "-t", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("tags").value() == "from_env");
    REQUIRE(parsed.get("monitor") == true);
  }

  // AT_MOST_ONE_ARG_N: the flag is typed with no value, so the variable supplies it instead of
  // the empty string FileConfigCommand previously saw.
  {
    const char   *argv[] = {"test_prog", "enable", "-c", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("cold").value() == "from_env");
  }

  // MORE_THAN_ONE_ARG_N: no value collected, so the variable supplies a single one instead of
  // "at least one argument expected".
  {
    const char   *argv[] = {"test_prog", "enable", "-p", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("params").size() == 1);
    REQUIRE(parsed.get("params")[0] == "from_env");
    REQUIRE(parsed.get("monitor") == true);
  }

  // A typed value always wins over the environment variable.
  {
    const char   *argv[] = {"test_prog", "enable", "-t", "cache", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("tags").value() == "cache");
  }

  // A typed value wins by itself, not just by count: a single typed value is not joined by the
  // variable too, for a fixed-arity option (-c, taking one value) or a repeated one (-p).
  {
    const char   *argv[] = {"test_prog", "enable", "-c", "file", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("cold").value() == "file");
    REQUIRE(parsed.get("cold").size() == 1);
  }
  {
    const char   *argv[] = {"test_prog", "enable", "-p", "a", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("params").size() == 1);
    REQUIRE(parsed.get("params")[0] == "a");
  }

  // An option is never activated by its declared variable alone: the variable only fills a
  // value for an option the caller already typed.
  {
    const char   *argv[] = {"test_prog", "enable", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("cold") == false);
    REQUIRE(parsed.get("tags") == false);
  }

  // The environment variable wins over the declared default.
  {
    TestArgParser default_parser;
    default_parser.add_global_usage("test_prog [OPTIONS]");
    ts::ArgParser::Command &dcmd = default_parser.add_command("enable", "enable debug");
    dcmd.add_option("--tags", "-t", "debug tags", "TS_ARGPARSER_TEST_FIXED", 1, "default_tags");

    const char   *argv[] = {"test_prog", "enable", "-t", nullptr};
    ts::Arguments parsed = default_parser.parse(argv);
    REQUIRE(parsed.get("tags").value() == "from_env");
  }

  // A command itself can declare a variable, resolved the same way an option's is.
  {
    TestArgParser command_parser;
    command_parser.add_global_usage("test_prog [OPTIONS]");
    command_parser.add_command("init", "init", "TS_ARGPARSER_TEST_FIXED", 1, nullptr);

    const char   *argv[] = {"test_prog", "init", nullptr};
    ts::Arguments parsed = command_parser.parse(argv);
    REQUIRE(parsed.get("init").value() == "from_env");
  }

  // Usage errors are unchanged when no variable is declared.
  {
    const char *argv[] = {"test_prog", "enable", "-u", "-m", nullptr};
    REQUIRE_THROWS(parser.parse(argv));
  }

  // Usage errors are unchanged when the variable is declared but unset.
  {
    const char *argv[] = {"test_prog", "enable", "-U", "-m", nullptr};
    REQUIRE_THROWS(parser.parse(argv));
  }

  // An empty token typed on the command line is a value, not an absent one: it must not be
  // filled from the declared variable, or it is left in the positional arguments.
  {
    const char *argv[] = {"test_prog", "get", "-t", "", "rec", nullptr};
    REQUIRE_THROWS(parser.parse(argv));
  }

  // Same, after "--" ends option recognition: the empty token is still a value, never a
  // variable fallback.
  {
    const char *argv[] = {"test_prog", "get", "-t", "--", "", "rec", nullptr};
    REQUIRE_THROWS(parser.parse(argv));
  }
}

TEST_CASE("The variable only applies when every occurrence of a repeated option is bare", "[parse][env]")
{
  setenv("TS_ARGPARSER_TEST_REPEAT", "from_env", 1);

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");

  ts::ArgParser::Command &cmd = parser.add_command("enable", "enable debug");
  cmd.add_option("--tags", "-t", "debug tags", "TS_ARGPARSER_TEST_REPEAT", 1);
  cmd.add_option("--params", "-p", "parameters", "TS_ARGPARSER_TEST_REPEAT", MORE_THAN_ONE_ARG_N);
  cmd.add_option("--pair", "-P", "a pair of values", "TS_ARGPARSER_TEST_REPEAT", 2);
  // No option in the tree combines this arity with a variable, so there is nothing to pin
  // against existing callers; this is here only to prove the exclusion itself.
  cmd.add_option("--directive", "-D", "directives", "TS_ARGPARSER_TEST_REPEAT", MORE_THAN_ZERO_ARG_N);

  // Two bare occurrences are no different from one: the variable still fills a single value.
  {
    const char   *argv[] = {"test_prog", "enable", "-t", "-t", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("tags").value() == "from_env");
    REQUIRE(parsed.get("tags").size() == 1);
  }

  // A bare occurrence next to a valued one, in every order and spelling, is the same usage
  // error the bare occurrence alone would have been without a variable at all (exit code 64,
  // not merely some exception, which a help or version exit would also throw in test mode).
  // The existing last-one-wins behaviour for an all-typed repeat (covered elsewhere) must not
  // apply here.
  {
    const char *argv[] = {"test_prog", "enable", "-t", "dns", "-t", nullptr};
    REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
  }
  {
    const char *argv[] = {"test_prog", "enable", "--tags=dns", "-t", nullptr};
    REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
  }
  {
    const char *argv[] = {"test_prog", "enable", "-t", "--tags=dns", nullptr};
    REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
  }
  {
    const char *argv[] = {"test_prog", "enable", "-t", "-t", "dns", nullptr};
    REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
  }

  // A line with no bare occurrence at all is untouched, including the existing last-one-wins
  // behaviour for a fixed-arity repeat, even though a variable is declared and set: the
  // variable must not also append itself after the typed value.
  {
    const char   *argv[] = {"test_prog", "enable", "-t", "a", "-t", "b", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("tags").value() == "b");
    REQUIRE(parsed.get("tags").size() == 1);
  }

  // MORE_THAN_ONE_ARG_N: a bare occurrence next to one that collected values is the same
  // mixed-usage error, not a silent single-value fill.
  {
    const char *argv[] = {"test_prog", "enable", "-p", "a", "-p", nullptr};
    REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
  }
  // Two bare occurrences of it still resolve to a single value from the variable.
  {
    const char   *argv[] = {"test_prog", "enable", "-p", "-p", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("params").size() == 1);
    REQUIRE(parsed.get("params")[0] == "from_env");
  }

  // A fixed arity above one: entirely bare fills every slot from the variable, but a partial
  // set is always a usage error, since the variable fills an option only when it has no
  // values at all.
  {
    const char   *argv[] = {"test_prog", "enable", "-P", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("pair").size() == 2);
    REQUIRE(parsed.get("pair")[0] == "from_env");
    REQUIRE(parsed.get("pair")[1] == "from_env");
  }
  {
    const char *argv[] = {"test_prog", "enable", "-P", "only_one", nullptr};
    REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
  }

  // MORE_THAN_ZERO_ARG_N does not support the fallback at all: zero values is already valid
  // input for it, so a bare occurrence stays empty rather than being filled from the variable.
  {
    const char   *argv[] = {"test_prog", "enable", "-D", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("directive") == true);
    REQUIRE(parsed.get("directive").size() == 0);
  }
}

namespace
{
// Parser-wide state lives at file scope in ArgParser.cc and outlives any one parser: a test
// that reaches the help path leaves usage_return_code at 0 for every test after it, and a test
// that sets a default command leaves that too. Clear it on the way in and on the way out.
class ArgParserStateGuard
{
public:
  ArgParserStateGuard() { reset(); }
  ~ArgParserStateGuard() { reset(); }
  static void
  reset()
  {
    Access::reset_global_state();
  }

private:
  struct Access : ts::ArgParser {
    using ts::ArgParser::reset_global_state;
  };
};
} // namespace

// Link-time check that the parser-wide state stays internal to ArgParser.cc: were any of these
// names exported from libtscore again, this binary would fail to link with duplicate symbols.
std::string global_usage;
std::string description;
std::string parser_program_name;
std::string default_command;
int         usage_return_code = 0;

TEST_CASE("Double dash keeps a value away from an outer command's options", "[parse]")
{
  ArgParserStateGuard guard;

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");
  parser.add_option("--format", "-f", "output format", "", 1, "", "format");
  parser.add_option("--help", "-h", "print usage");
  parser.add_option("--version", "-V", "print version");

  // Every level has options of its own, so the escape has to hold in each level's scan.
  ts::ArgParser::Command &config = parser.add_command("config", "configuration commands");
  config.add_option("--mid", "-m", "an option of the intermediate command", "", 1, "", "mid");
  config.add_command("set", "set a record", "", 2, nullptr, "set")
    .add_option("--type", "-t", "an option of the owning command", "", 1, "", "type");
  config.add_command("reload", "reload configs", nullptr, "reload")
    .add_option("--directive", "-D", "reload directives", "", MORE_THAN_ZERO_ARG_N, "", "directive");

  SECTION("a value spelled like an outer option belongs to the subcommand that asked for it")
  {
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "-f", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[0] == "a.record");
    REQUIRE(parsed.get("set")[1] == "-f");
    REQUIRE(parsed.get("format") == false);
  }

  SECTION("including one that would otherwise print the help message and exit 0")
  {
    // Without the escape the command never runs, yet a script sees success.
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "-h", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "-h");
  }

  SECTION("or the version message")
  {
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "-V", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "-V");
  }

  SECTION("a value spelled like an option of the intermediate command is left to the subcommand")
  {
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "-m");
    REQUIRE(parsed.get("mid") == false);
  }

  SECTION("so is one spelled like an option of the owning command itself")
  {
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "-t", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "-t");
    REQUIRE(parsed.get("type") == false);
  }

  SECTION("and one in the --option=value form")
  {
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "--format=json", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "--format=json");
    REQUIRE(parsed.get("format") == false);
  }

  SECTION("a second double dash is itself the escaped value")
  {
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "--", "-f", "json", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "--");
    REQUIRE(parsed.get("format").value() == "json");
  }

  SECTION("an outer option written after the escaped value is still recognized")
  {
    // "--" escapes one token, it does not end the command line.
    const char   *argv[] = {"test_prog", "config", "set", "a.record", "--", "-x", "-f", "json", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "-x");
    REQUIRE(parsed.get("format").value() == "json");
  }

  SECTION("an outer option before the subcommand keeps working")
  {
    // The whole command line has been scanned at every level since the parser was written.
    const char   *argv[] = {"test_prog", "-f", "json", "config", "set", "a.record", "value", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "json");
    REQUIRE(parsed.get("set").size() == 2);
    REQUIRE(parsed.get("set")[1] == "value");
  }

  SECTION("only the first value after the double dash is protected for a variable number of values")
  {
    // The escape covers one token in the outer scans, whatever the arity of the option that owns
    // it, so a later token spelled like an outer option is still that option.
    const char   *argv[] = {"test_prog", "config", "reload", "-D", "--", "-a", "-f", "json", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("directive").size() == 1);
    REQUIRE(parsed.get("directive")[0] == "-a");
    REQUIRE(parsed.get("format").value() == "json");
  }

  SECTION("a trailing double dash with nothing to escape is still an unknown argument")
  {
    // The skip at the last token leaves it over, as before, rather than escaping past the end.
    const char *argv[] = {"test_prog", "config", "set", "a.record", "value", "--", nullptr};
    REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
  }
}

TEST_CASE("The default command retry replaces the failed pass's values", "[parse]")
{
  ArgParserStateGuard guard;

  {
    TestArgParser parser;
    parser.add_global_usage("test_prog [OPTIONS]");
    parser.add_option("--zed", "-z", "values", "", MORE_THAN_ZERO_ARG_N, "", "zed");
    parser.add_command("run", "run it", nullptr, "run");
    parser.set_default_command("run");

    // No command is named, so the first pass fails after the top level has already collected
    // --zed. The retry inserts the default command and collects --zed again; the values of the
    // failed pass must be replaced by it, not accumulated on top of.
    const char   *argv[] = {"test_prog", "-z", "a", "b", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("run") == true);
    REQUIRE(parsed.get("zed").size() == 2);
    REQUIRE(parsed.get("zed")[0] == "a");
    REQUIRE(parsed.get("zed")[1] == "b");
  }

  ArgParserStateGuard::reset();

  // With the state cleared, a later parser has no default command: a line that names no
  // command is not retried with one.
  {
    TestArgParser parser;
    parser.add_global_usage("test_prog [OPTIONS]");
    parser.add_command("run", "run it", nullptr, "run");

    const char   *argv[] = {"test_prog", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("run") == false);
  }
}

TEST_CASE("reset_global_state clears every piece of parser-wide state", "[parse]")
{
  ArgParserStateGuard guard;

  // Keeps the help and usage text that these parsers print out of the test output, and lets it
  // be checked.
  struct CoutCapture {
    std::ostringstream buf;
    std::streambuf    *old = std::cout.rdbuf(buf.rdbuf());
    ~CoutCapture() { std::cout.rdbuf(old); }
  };

  SECTION("the exit code a usage error returns, which a help request sets to 0")
  {
    {
      TestArgParser parser;
      parser.add_option("--help", "-h", "print usage");
      parser.add_command("run", "run it", nullptr, "run");

      const char *argv[] = {"test_prog", "-h", nullptr};
      CoutCapture capture;
      REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 0");
    }
    ArgParserStateGuard::reset();
    {
      TestArgParser parser;
      parser.add_command("run", "run it", nullptr, "run");

      const char *argv[] = {"test_prog", "bogus", nullptr};
      CoutCapture capture;
      REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
    }
  }

  SECTION("the description and global usage an earlier parser added")
  {
    {
      TestArgParser parser;
      parser.add_description("earlier parser's description");
      parser.add_global_usage("earlier parser's usage");
    }
    ArgParserStateGuard::reset();

    TestArgParser parser;
    parser.add_command("run", "run it", nullptr, "run");

    const char *argv[] = {"test_prog", "bogus", nullptr};
    std::string out;
    {
      CoutCapture capture;
      REQUIRE_THROWS_WITH(parser.parse(argv), "Test mode: exit with code 64");
      out = capture.buf.str();
    }
    REQUIRE(out.find("Unknown command") != std::string::npos);
    REQUIRE(out.find("earlier parser's description") == std::string::npos);
    REQUIRE(out.find("earlier parser's usage") == std::string::npos);
  }
}

namespace
{
// The message a line's usage error prints, so a test can tell one exit code 64 from another.
std::string
usage_error(ts::ArgParser &parser, const char **argv)
{
  std::ostringstream out;
  std::streambuf    *old = std::cout.rdbuf(out.rdbuf());
  std::string        exit_message;

  try {
    parser.parse(argv);
  } catch (std::runtime_error const &e) {
    exit_message = e.what();
  }
  std::cout.rdbuf(old);
  REQUIRE(exit_message == "Test mode: exit with code 64");
  return out.str();
}

bool
contains(std::string const &text, std::string const &part)
{
  return text.find(part) != std::string::npos;
}

// The usage error for a value an option took though it names an option of a command named earlier
// on the line. For an option taking a variable number of values a "--" would take the rest of the
// line too, so the hint is the --option=value form instead.
std::string
swallowed(std::string const &token, std::string const &command, std::string const &option, bool variable = false)
{
  std::string const hint = variable ? "write " + option + "=" + token + " to make it one" : "put -- before it to make it one";

  return "'" + token + "' is an option of " + command + ", not a value of " + option + " (" + hint + ")";
}

// How a line that answers with a message, such as its help, instead of parsing ends.
std::string
parse_exit(ts::ArgParser &parser, const char **argv)
{
  std::ostringstream out;
  std::streambuf    *old = std::cout.rdbuf(out.rdbuf());
  std::string        message;

  try {
    parser.parse(argv);
  } catch (std::runtime_error const &e) {
    message = e.what();
  }
  std::cout.rdbuf(old);
  return message;
}
} // namespace

TEST_CASE("An option's value is not taken from an option of a command named earlier on the line", "[parse]")
{
  ArgParserStateGuard guard;

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");
  parser.add_option("--format", "-f", "output format", "", 1, "", "format");
  parser.add_option("--level", "-l", "a level, which may be left out", "", AT_MOST_ONE_ARG_N, "", "level");
  parser.add_option("--tags", "-T", "tags", "", MORE_THAN_ZERO_ARG_N, "", "tags");
  parser.add_option("--pair", "-P", "two values", "", 2, "", "pair");
  parser.add_option("--list", "-L", "one value or more", "", MORE_THAN_ONE_ARG_N, "", "list");
  parser.add_option("--help", "-h", "help");
  parser.add_option("--version", "-V", "version");

  ts::ArgParser::Command &config = parser.add_command("config", "configuration commands");
  config.add_option("--scope", "-c", "an option of the command in the middle of the line");
  config.add_option("--section", "-S", "an option of the command in the middle of the line taking one value", "", 1, "", "section");
  config.add_option("--both", "-b", "an option config and reload both have");
  config.add_command("reload", "reload configs", nullptr, "reload")
    .add_option("--monitor", "-m", "monitor progress")
    .add_option("--both", "-b", "an option config and reload both have", "", 0, "", "reload_both")
    .add_option("--token", "-t", "a token", "", 1, "", "token");
  config.add_command("get", "get records", "", MORE_THAN_ONE_ARG_N, nullptr, "get")
    .add_option("--json", "-j", "an option only config get has");
  config.add_command("status", "show the status", nullptr, "status");

  SECTION("a subcommand's option is not swallowed as the value of an outer option")
  {
    const char *argv[] = {"test_prog", "config", "reload", "-f", "-m", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--format")));
  }

  SECTION("nor by its long name")
  {
    const char *argv[] = {"test_prog", "config", "reload", "-f", "--monitor", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("--monitor", "reload", "--format")));
  }

  SECTION("nor in its --option=value form")
  {
    const char *argv[] = {"test_prog", "config", "reload", "-f", "--token=abc", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("--token=abc", "reload", "--format")));
  }

  SECTION("whatever the number of values the outer option takes")
  {
    {
      const char *argv[] = {"test_prog", "config", "reload", "-l", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--level")));
    }
    {
      const char *argv[] = {"test_prog", "config", "reload", "-T", "a", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--tags", true)));
    }
    {
      const char *argv[] = {"test_prog", "config", "reload", "-L", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--list", true)));
    }
    {
      const char *argv[] = {"test_prog", "config", "reload", "-P", "a", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--pair")));
    }
    {
      const char *argv[] = {"test_prog", "config", "reload", "-P", "-m", "a", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--pair")));
    }
  }

  SECTION("tokens taken off the line before the subcommand's name do not hide it")
  {
    // Each command takes its own options off the line before its subcommands are read, so the
    // position that counts is the one on the command line, not the one left in what remains.
    {
      const char *argv[] = {"test_prog", "-f", "json", "config", "reload", "-f", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--format")));
    }
    {
      const char *argv[] = {"test_prog", "--tags=a", "--tags=b", "config", "reload", "-f", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--format")));
    }
    {
      const char *argv[] = {"test_prog", "-f", "x", "config", "reload", "-S", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--section")));
    }
  }

  SECTION("nor by an option of the command in the middle of the line")
  {
    const char *argv[] = {"test_prog", "config", "reload", "-S", "-m", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--section")));
  }

  SECTION("an option of the command in the middle of the line counts too")
  {
    const char *argv[] = {"test_prog", "config", "reload", "-f", "-c", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-c", "config", "--format")));
  }

  SECTION("a token naming an option of two commands on the line is reported for the outer one")
  {
    const char *argv[] = {"test_prog", "config", "reload", "-f", "-b", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-b", "config", "--format")));
  }

  SECTION("one written before the subcommand's name is still a value, since it could never reach the subcommand there")
  {
    const char   *argv[] = {"test_prog", "-f", "-m", "config", "reload", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
    REQUIRE(parsed.get("reload") == true);
    REQUIRE(parsed.get("monitor") == false);
  }

  SECTION("an option of a command that is not on the line is still a value")
  {
    // Only the commands actually dispatched count, so adding an option to one command does not
    // change how a line that names another command is read.
    {
      const char   *argv[] = {"test_prog", "config", "reload", "-f", "-j", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("format").value() == "-j");
    }
    {
      const char   *argv[] = {"test_prog", "config", "status", "-f", "-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("format").value() == "-m");
    }
    {
      const char   *argv[] = {"test_prog", "-f", "-j", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("format").value() == "-j");
    }
  }

  SECTION("so is a token that names no option at all")
  {
    const char   *argv[] = {"test_prog", "config", "reload", "-f", "-weird", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-weird");
  }

  SECTION("a value spelled like a subcommand's option can still be passed on purpose")
  {
    {
      const char   *argv[] = {"test_prog", "config", "reload", "-f", "--", "-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("format").value() == "-m");
      REQUIRE(parsed.get("monitor") == false);
    }
    {
      const char   *argv[] = {"test_prog", "config", "reload", "--format=-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("format").value() == "-m");
      REQUIRE(parsed.get("monitor") == false);
    }
    {
      const char   *argv[] = {"test_prog", "config", "reload", "-l", "--", "-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("level").value() == "-m");
    }
    {
      const char   *argv[] = {"test_prog", "config", "reload", "-P", "a", "--", "-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("pair")[1] == "-m");
    }
    {
      const char   *argv[] = {"test_prog", "config", "reload", "-T", "a", "--", "-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("tags").size() == 2);
      REQUIRE(parsed.get("tags")[1] == "-m");
    }
  }

  SECTION("the hint for an option taking a variable number of values keeps the rest of the line")
  {
    // A "--" there would take -t and its value as tags too.
    const char   *argv[] = {"test_prog", "config", "reload", "-T", "a", "--tags=-m", "-t", "x", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("tags").size() == 2);
    REQUIRE(parsed.get("tags")[1] == "-m");
    REQUIRE(parsed.get("token").value() == "x");
  }

  SECTION("a line refused for another reason keeps its own error")
  {
    const char *argv[] = {"test_prog", "config", "reload", "-f", "-m", "-t", nullptr};
    std::string out    = usage_error(parser, argv);
    REQUIRE(contains(out, "1 argument(s) expected by token"));
    REQUIRE(!contains(out, "is an option of"));
  }

  SECTION("a line asking for help or the version still gets it")
  {
    {
      const char *argv[] = {"test_prog", "config", "reload", "-f", "-m", "-h", nullptr};
      REQUIRE(parse_exit(parser, argv) == "Test mode: exit with code 0");
    }
    {
      const char *argv[] = {"test_prog", "config", "reload", "-f", "-m", "--version", nullptr};
      REQUIRE(parse_exit(parser, argv) == "Test mode: exit with code 0");
    }
  }
}

TEST_CASE("Only a command the line dispatches counts, wherever its name appears", "[parse]")
{
  ArgParserStateGuard guard;

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");
  parser.add_option("--format", "-f", "output format", "", 1, "", "format");
  ts::ArgParser::Command &config = parser.add_command("config", "configuration commands");
  config.add_option("--section", "-S", "an option of config taking one value", "", 1, "", "section");
  config.add_option("--pair", "-Q", "an option of config taking two values", "", 2, "", "pair");
  config.add_option("--vary", "-V", "an option of config taking any number of values", "", MORE_THAN_ZERO_ARG_N, "", "vary");
  config.add_option("--maybe", "-O", "an option of config taking at most one value", "", AT_MOST_ONE_ARG_N, "", "maybe");
  config.add_command("reload", "reload configs", nullptr, "reload")
    .add_option("--monitor", "-m", "monitor progress")
    .add_option("--token", "-t", "a token", "", 1, "", "token");
  config.add_command("status", "show the status", nullptr, "status");

  SECTION("a value spelled like a subcommand does not name it")
  {
    // "reload" is --section's value; config status is the command that runs, and -m is not its option.
    const char   *argv[] = {"test_prog", "config", "-S", "reload", "status", "-f", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
    REQUIRE(parsed.get("section").value() == "reload");
    REQUIRE(parsed.get("status") == true);
  }

  SECTION("nor when that option is spelled by its long name")
  {
    const char   *argv[] = {"test_prog", "config", "--section", "reload", "status", "-f", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
  }

  SECTION("nor any of its values, when it takes more than one")
  {
    const char   *argv[] = {"test_prog", "config", "-Q", "a", "reload", "status", "-f", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
    REQUIRE(parsed.get("pair").size() == 2);
    REQUIRE(parsed.get("pair")[1] == "reload");
  }

  SECTION("nor when it takes at most one")
  {
    const char   *argv[] = {"test_prog", "config", "-O", "reload", "status", "-f", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
    REQUIRE(parsed.get("maybe").value() == "reload");
    REQUIRE(parsed.get("status") == true);
  }

  SECTION("nor when the number of values it takes varies")
  {
    const char   *argv[] = {"test_prog", "config", "-V", "x", "reload", "-f", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
    REQUIRE(parsed.get("vary").size() == 2);
    REQUIRE(parsed.get("vary")[1] == "reload");
    REQUIRE(parsed.get("reload") == false);
  }

  SECTION("nor one escaped by a -- among its values")
  {
    const char   *argv[] = {"test_prog", "config", "-S", "--", "reload", "status", "-f", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
    REQUIRE(parsed.get("section").value() == "reload");
  }

  SECTION("but only the first -- among them is an escape; a second one is the value")
  {
    const char *argv[] = {"test_prog", "config", "-S", "--", "--", "reload", "-f", "-m", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--format")));
  }

  SECTION("a value spelled like a subcommand that has the option is not taken for that subcommand")
  {
    // -t is reload's option, but reload is only --section's value here.
    const char   *argv[] = {"test_prog", "config", "-S", "reload", "-f", "-t", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-t");
  }
}

TEST_CASE("A command with a value and a subcommand", "[parse]")
{
  ArgParserStateGuard guard;

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");
  parser.add_option("--format", "-f", "output format", "", 1, "", "format");
  parser.add_command("cmd", "a command with a value and a subcommand", "", 1, nullptr, "cmd")
    .add_command("sub", "its subcommand", nullptr, "sub")
    .add_option("--sopt", "-s", "an option of the subcommand");

  SECTION("its value comes before any subcommand's name, so it may be spelled like the subcommand's option")
  {
    const char   *argv[] = {"test_prog", "cmd", "-s", "sub", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("cmd").size() == 1);
    REQUIRE(parsed.get("cmd")[0] == "-s");
    REQUIRE(parsed.get("sub") == true);
  }

  SECTION("a value spelled like its subcommand is its value, not the subcommand")
  {
    const char   *argv[] = {"test_prog", "cmd", "sub", "-f", "-s", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-s");
    REQUIRE(parsed.get("cmd")[0] == "sub");
    REQUIRE(parsed.get("sub") == false);
  }

  SECTION("and a second one is the subcommand")
  {
    const char *argv[] = {"test_prog", "cmd", "sub", "sub", "-f", "-s", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-s", "sub", "--format")));
  }

  SECTION("a subcommand name escaped by -- is its value, not a subcommand")
  {
    const char   *argv[] = {"test_prog", "cmd", "--", "sub", "-f", "-s", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-s");
    REQUIRE(parsed.get("cmd").size() == 1);
    REQUIRE(parsed.get("cmd")[0] == "sub");
    REQUIRE(parsed.get("sub") == false);
  }

  SECTION("a subcommand named after an escaped value still counts")
  {
    const char *argv[] = {"test_prog", "cmd", "--", "-v", "sub", "-f", "-s", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-s", "sub", "--format")));
  }
}

TEST_CASE("A subcommand sharing its parent's name counts only where it is named", "[parse]")
{
  ArgParserStateGuard guard;

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");
  parser.add_command("a", "the parent", nullptr, "parent")
    .add_option("--opt", "-o", "an option of the parent", "", 1, "", "opt")
    .add_command("a", "a child with the parent's name", nullptr, "child")
    .add_option("--xopt", "-x", "an option of the child");

  SECTION("the parent's own name is not the child's")
  {
    const char   *argv[] = {"test_prog", "a", "-o", "-x", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("opt").value() == "-x");
  }

  SECTION("a second one names the child")
  {
    const char *argv[] = {"test_prog", "a", "a", "-o", "-x", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-x", "a", "--opt")));
  }
}

TEST_CASE("A default command counts only when it is written out", "[parse]")
{
  ArgParserStateGuard guard;

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");
  parser.add_option("--format", "-f", "output format", "", 1, "", "format");
  ts::ArgParser::Command &reload = parser.add_command("reload", "reload configs", nullptr, "reload");
  reload.add_option("--monitor", "-m", "monitor progress").add_option("--target", "-g", "a target", "", 1, "", "target");
  reload.add_command("all", "reload everything", nullptr, "all").add_option("--wait", "-w", "wait for it");
  parser.add_command("init", "initialize", nullptr, "init").add_option("--init-opt", "-i", "an option of init");
  parser.set_default_command("reload");

  SECTION("the one the parser fills in for a line that names no command does not count")
  {
    const char   *argv[] = {"test_prog", "-f", "-m", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-m");
    REQUIRE(parsed.get("reload") == true);
    REQUIRE(parsed.get("monitor") == false);
  }

  SECTION("written out, it counts like any other")
  {
    const char *argv[] = {"test_prog", "reload", "-f", "-m", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--format")));
  }

  SECTION("a subcommand of it that the line does write counts")
  {
    const char *argv[] = {"test_prog", "all", "-f", "-w", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-w", "all", "--format")));
  }

  SECTION("a command name taken as a value on the way to the default command does not count")
  {
    // The first pass dispatches nothing, so the line is read again as the default command's.
    const char   *argv[] = {"test_prog", "-g", "init", "-f", "-i", nullptr};
    ts::Arguments parsed = parser.parse(argv);
    REQUIRE(parsed.get("format").value() == "-i");
    REQUIRE(parsed.get("target").value() == "init");
    REQUIRE(parsed.get("init") == false);
  }

  SECTION("nothing carries over from one line to the next")
  {
    {
      const char   *argv[] = {"test_prog", "-f", "-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("format").value() == "-m");
    }
    {
      const char *argv[] = {"test_prog", "reload", "-f", "-m", nullptr};
      REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--format")));
    }
    {
      const char   *argv[] = {"test_prog", "-f", "-m", nullptr};
      ts::Arguments parsed = parser.parse(argv);
      REQUIRE(parsed.get("format").value() == "-m");
    }
  }
}

TEST_CASE("A declared variable does not stand in for a value written for a subcommand", "[parse][env]")
{
  ArgParserStateGuard guard;
  setenv("TS_ARGPARSER_TEST_NAMED_ROOT", "root_env", 1);
  setenv("TS_ARGPARSER_TEST_NAMED_LEVEL", "level_env", 1);
  setenv("TS_ARGPARSER_TEST_NAMED_LIST", "list_env", 1);

  TestArgParser parser;
  parser.add_global_usage("test_prog [OPTIONS]");
  parser.add_option("--root", "-r", "a root", "TS_ARGPARSER_TEST_NAMED_ROOT", 1, "", "root");
  parser.add_option("--level", "-l", "a level", "TS_ARGPARSER_TEST_NAMED_LEVEL", AT_MOST_ONE_ARG_N, "", "level");
  parser.add_option("--list", "-L", "a list", "TS_ARGPARSER_TEST_NAMED_LIST", MORE_THAN_ONE_ARG_N, "", "list");
  parser.add_command("reload", "reload configs", nullptr, "reload").add_option("--monitor", "-m", "monitor progress");

  // The line is refused, as it is without the variable, rather than the variable filling the
  // option while the subcommand's option is quietly applied.
  SECTION("for a fixed number of values")
  {
    const char *argv[] = {"test_prog", "reload", "-r", "-m", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--root")));
  }

  SECTION("for at most one value")
  {
    const char *argv[] = {"test_prog", "reload", "-l", "-m", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--level")));
  }

  SECTION("for one value or more")
  {
    const char *argv[] = {"test_prog", "reload", "-L", "-m", nullptr};
    REQUIRE(contains(usage_error(parser, argv), swallowed("-m", "reload", "--list", true)));
  }

  unsetenv("TS_ARGPARSER_TEST_NAMED_ROOT");
  unsetenv("TS_ARGPARSER_TEST_NAMED_LEVEL");
  unsetenv("TS_ARGPARSER_TEST_NAMED_LIST");
}
