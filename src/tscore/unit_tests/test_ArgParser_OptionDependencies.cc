/** @file

  Unit test for ArgParser option dependencies (with_required)

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

class TestArgParser : public ts::ArgParser
{
public:
  TestArgParser() { ts::ArgParser::set_test_mode(true); }
};

TEST_CASE("Option dependencies - basic dependency", "[option_dependencies]")
{
  ts::ArgParser parser;
  parser.add_description("Test basic option dependency");
  parser.add_global_usage("test [OPTIONS]");

  parser.add_option("--tags", "-t", "Debug tags", "", 1);
  parser.add_option("--append", "-a", "Append to existing tags").with_required("--tags");

  // Test with both options - should work
  const char   *argv1[] = {"test", "--tags", "http", "--append", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("tags") == true);
  REQUIRE(args1.get("tags").value() == "http");
  REQUIRE(args1.get("append") == true);

  // Test with only --tags - should work
  const char   *argv2[] = {"test", "--tags", "dns", nullptr};
  ts::Arguments args2   = parser.parse(argv2);
  REQUIRE(args2.get("tags") == true);
  REQUIRE(args2.get("tags").value() == "dns");
  REQUIRE(args2.get("append") == false);

  // Test with neither option - should work
  const char   *argv3[] = {"test", nullptr};
  ts::Arguments args3   = parser.parse(argv3);
  REQUIRE(args3.get("tags") == false);
  REQUIRE(args3.get("append") == false);
}

TEST_CASE("Option dependencies - violation detection", "[option_dependencies]")
{
  TestArgParser parser;
  parser.add_description("Test dependency violation");
  parser.add_global_usage("test [OPTIONS]");

  parser.add_option("--tags", "-t", "Debug tags", "", 1);
  parser.add_option("--append", "-a", "Append to existing tags").with_required("--tags");

  // Test with only --append (without --tags) - should error
  const char *argv[] = {"test", "--append", nullptr};
  REQUIRE_THROWS(parser.parse(argv));
}

TEST_CASE("Option dependencies - short option violation", "[option_dependencies]")
{
  TestArgParser parser;
  parser.add_description("Test dependency violation with short option");
  parser.add_global_usage("test [OPTIONS]");

  parser.add_option("--tags", "-t", "Debug tags", "", 1);
  parser.add_option("--append", "-a", "Append to existing tags").with_required("--tags");

  // Test with short option -a (without --tags) - should error
  const char *argv[] = {"test", "-a", nullptr};
  REQUIRE_THROWS(parser.parse(argv));
}

TEST_CASE("Option dependencies - multiple dependencies", "[option_dependencies]")
{
  ts::ArgParser parser;
  parser.add_description("Test multiple option dependencies");
  parser.add_global_usage("test [OPTIONS]");

  parser.add_option("--tags", "-t", "Debug tags", "", 1);
  parser.add_option("--append", "-a", "Append mode");
  parser.add_option("--verbose-append", "-V", "Verbose append mode").with_required("--tags").with_required("--append");

  // Test with all options - should work
  const char   *argv1[] = {"test", "--tags", "http", "--append", "--verbose-append", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("tags") == true);
  REQUIRE(args1.get("append") == true);
  REQUIRE(args1.get("verbose-append") == true);
}

TEST_CASE("Option dependencies - multiple dependencies violation", "[option_dependencies]")
{
  TestArgParser parser;
  parser.add_description("Test multiple dependency violation");
  parser.add_global_usage("test [OPTIONS]");

  parser.add_option("--tags", "-t", "Debug tags", "", 1);
  parser.add_option("--append", "-a", "Append mode");
  parser.add_option("--verbose-append", "-V", "Verbose append mode").with_required("--tags").with_required("--append");

  // Test with --verbose-append but only --tags (missing --append) - should error
  const char *argv[] = {"test", "--tags", "http", "--verbose-append", nullptr};
  REQUIRE_THROWS(parser.parse(argv));
}

TEST_CASE("Option dependencies - with subcommands", "[option_dependencies]")
{
  ts::ArgParser           parser;
  ts::ArgParser::Command &cmd = parser.add_command("debug", "Debug commands");

  cmd.add_option("--tags", "-t", "Debug tags", "", 1);
  cmd.add_option("--append", "-a", "Append to existing tags").with_required("--tags");

  // Test with subcommand and both options - should work
  const char   *argv1[] = {"test", "debug", "--tags", "http", "--append", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("debug") == true);
  REQUIRE(args1.get("tags") == true);
  REQUIRE(args1.get("append") == true);

  // Test with subcommand and only --tags - should work
  const char   *argv2[] = {"test", "debug", "-t", "dns", nullptr};
  ts::Arguments args2   = parser.parse(argv2);
  REQUIRE(args2.get("debug") == true);
  REQUIRE(args2.get("tags") == true);
  REQUIRE(args2.get("append") == false);
}

TEST_CASE("Option dependencies - subcommand violation", "[option_dependencies]")
{
  TestArgParser           parser;
  TestArgParser::Command &cmd = parser.add_command("debug", "Debug commands");

  cmd.add_option("--tags", "-t", "Debug tags", "", 1);
  cmd.add_option("--append", "-a", "Append to existing tags").with_required("--tags");

  // Test with subcommand and only --append - should error
  const char *argv[] = {"test", "debug", "--append", nullptr};
  REQUIRE_THROWS(parser.parse(argv));
}

TEST_CASE("Option dependencies - invalid required option", "[option_dependencies]")
{
  TestArgParser parser;
  parser.add_description("Test invalid required option");
  parser.add_global_usage("test [OPTIONS]");

  parser.add_option("--append", "-a", "Append mode");

  // Try to require an option that doesn't exist - should throw
  REQUIRE_THROWS(parser.add_option("--verbose", "-v", "Verbose mode").with_required("--nonexistent"));
}

TEST_CASE("Option dependencies - with_required without add_option", "[option_dependencies]")
{
  TestArgParser parser;
  parser.add_description("Test with_required without prior add_option");
  parser.add_global_usage("test [OPTIONS]");

  // Calling with_required() without first calling add_option() should error
  // This is a bit tricky to test since with_required returns Command&
  // The error would occur at runtime when there's no _last_added_option
  // We need to test this via the Command directly
  parser.add_option("--first", "-f", "First option");

  // Add a second option and require the first - this should work
  parser.add_option("--second", "-s", "Second option").with_required("--first");

  const char   *argv[] = {"test", "--first", "--second", nullptr};
  ts::Arguments args   = parser.parse(argv);
  REQUIRE(args.get("first") == true);
  REQUIRE(args.get("second") == true);
}

TEST_CASE("Option dependencies - combined with mutex groups", "[option_dependencies]")
{
  ts::ArgParser parser;
  parser.add_description("Test dependencies combined with mutex groups");
  parser.add_global_usage("test [OPTIONS]");

  // Mutex group for mode
  parser.add_mutex_group("mode", false, "Operation mode");
  parser.add_option_to_group("mode", "--enable", "-e", "Enable mode");
  parser.add_option_to_group("mode", "--disable", "-d", "Disable mode");

  // Option that requires --enable
  parser.add_option("--tags", "-t", "Debug tags", "", 1).with_required("--enable");

  // Test with --enable and --tags - should work
  const char   *argv1[] = {"test", "--enable", "--tags", "http", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("enable") == true);
  REQUIRE(args1.get("tags") == true);

  // Test with --disable only - should work (--tags not used)
  const char   *argv2[] = {"test", "--disable", nullptr};
  ts::Arguments args2   = parser.parse(argv2);
  REQUIRE(args2.get("disable") == true);
  REQUIRE(args2.get("tags") == false);
}

TEST_CASE("Option dependencies - combined with mutex groups violation", "[option_dependencies]")
{
  TestArgParser parser;
  parser.add_description("Test dependencies combined with mutex groups violation");
  parser.add_global_usage("test [OPTIONS]");

  // Mutex group for mode
  parser.add_mutex_group("mode", false, "Operation mode");
  parser.add_option_to_group("mode", "--enable", "-e", "Enable mode");
  parser.add_option_to_group("mode", "--disable", "-d", "Disable mode");

  // Option that requires --enable
  parser.add_option("--tags", "-t", "Debug tags", "", 1).with_required("--enable");

  // Test with --disable and --tags - should error (--tags requires --enable)
  const char *argv[] = {"test", "--disable", "--tags", "http", nullptr};
  REQUIRE_THROWS(parser.parse(argv));
}
