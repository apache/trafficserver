/** @file

  Unit test for ArgParser mutually exclusive groups

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

TEST_CASE("Mutex groups - optional group", "[mutex_groups]")
{
  ts::ArgParser parser;
  parser.add_description("Test optional mutex group");
  parser.add_global_usage("test [OPTIONS]");

  // Create an OPTIONAL mutex group for verbosity
  parser.add_mutex_group("verbosity", false, "Verbosity level");
  parser.add_option_to_group("verbosity", "--verbose", "-v", "Enable verbose output");
  parser.add_option_to_group("verbosity", "--quiet", "-q", "Suppress output");

  // Test with --verbose
  const char   *argv1[] = {"test", "--verbose", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("verbose") == true);
  REQUIRE(args1.get("quiet") == false);

  // Test with --quiet
  const char   *argv2[] = {"test", "--quiet", nullptr};
  ts::Arguments args2   = parser.parse(argv2);
  REQUIRE(args2.get("verbose") == false);
  REQUIRE(args2.get("quiet") == true);

  // Test with no option (optional group, so this is valid)
  const char   *argv3[] = {"test", nullptr};
  ts::Arguments args3   = parser.parse(argv3);
  REQUIRE(args3.get("verbose") == false);
  REQUIRE(args3.get("quiet") == false);

  // Test with short options
  const char   *argv4[] = {"test", "-v", nullptr};
  ts::Arguments args4   = parser.parse(argv4);
  REQUIRE(args4.get("verbose") == true);
}

TEST_CASE("Mutex groups - required group", "[mutex_groups]")
{
  ts::ArgParser parser;
  parser.add_description("Test required mutex group");
  parser.add_global_usage("test [OPTIONS]");
  // Create a REQUIRED mutex group for output format
  parser.add_mutex_group("format", true, "Output format (required)");
  parser.add_option_to_group("format", "--json", "-j", "Output in JSON format");
  parser.add_option_to_group("format", "--xml", "-x", "Output in XML format");
  parser.add_option_to_group("format", "--yaml", "-y", "Output in YAML format");

  // Test with --json
  const char   *argv1[] = {"test", "--json", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("json") == true);
  REQUIRE(args1.get("xml") == false);
  REQUIRE(args1.get("yaml") == false);

  // Test with --xml
  const char   *argv2[] = {"test", "--xml", nullptr};
  ts::Arguments args2   = parser.parse(argv2);
  REQUIRE(args2.get("json") == false);
  REQUIRE(args2.get("xml") == true);
  REQUIRE(args2.get("yaml") == false);

  // Test with short option
  const char   *argv3[] = {"test", "-y", nullptr};
  ts::Arguments args3   = parser.parse(argv3);
  REQUIRE(args3.get("yaml") == true);
}

TEST_CASE("Mutex groups - combined with regular options", "[mutex_groups]")
{
  ts::ArgParser parser;
  parser.add_description("Test mutex groups with regular options");
  parser.add_global_usage("test [OPTIONS]");

  // Mutex group
  parser.add_mutex_group("format", false, "Output format");
  parser.add_option_to_group("format", "--json", "-j", "Output in JSON format");
  parser.add_option_to_group("format", "--xml", "-x", "Output in XML format");

  // Regular option
  parser.add_option("--output", "-o", "Output file", "", 1);

  // Test with both mutex group option and regular option
  const char   *argv1[] = {"test", "--json", "--output", "file.txt", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("json") == true);
  REQUIRE(args1.get("output") == true);
  REQUIRE(args1.get("output").value() == "file.txt");

  // Test with just regular option
  const char   *argv2[] = {"test", "-o", "output.log", nullptr};
  ts::Arguments args2   = parser.parse(argv2);
  REQUIRE(args2.get("json") == false);
  REQUIRE(args2.get("xml") == false);
  REQUIRE(args2.get("output") == true);
  REQUIRE(args2.get("output").value() == "output.log");
}

TEST_CASE("Mutex groups - multiple groups", "[mutex_groups]")
{
  ts::ArgParser parser;
  parser.add_description("Test multiple mutex groups");
  parser.add_global_usage("test [OPTIONS]");

  // First mutex group
  parser.add_mutex_group("format", false, "Output format");
  parser.add_option_to_group("format", "--json", "-j", "Output in JSON format");
  parser.add_option_to_group("format", "--xml", "-x", "Output in XML format");

  // Second mutex group
  parser.add_mutex_group("verbosity", false, "Verbosity level");
  parser.add_option_to_group("verbosity", "--verbose", "-v", "Enable verbose output");
  parser.add_option_to_group("verbosity", "--quiet", "-q", "Suppress output");

  // Test with one option from each group
  const char   *argv1[] = {"test", "--json", "--verbose", nullptr};
  ts::Arguments args1   = parser.parse(argv1);
  REQUIRE(args1.get("json") == true);
  REQUIRE(args1.get("xml") == false);
  REQUIRE(args1.get("verbose") == true);
  REQUIRE(args1.get("quiet") == false);

  // Test with different options from each group
  const char   *argv2[] = {"test", "-x", "-q", nullptr};
  ts::Arguments args2   = parser.parse(argv2);
  REQUIRE(args2.get("xml") == true);
  REQUIRE(args2.get("json") == false);
  REQUIRE(args2.get("quiet") == true);
  REQUIRE(args2.get("verbose") == false);
}
class TestArgParser : public ts::ArgParser
{
public:
  TestArgParser() { ts::ArgParser::set_test_mode(true); }
};

TEST_CASE("Mutex groups - violation detection", "[mutex_groups]")
{
  TestArgParser parser;
  parser.add_mutex_group("format", false, "Output format");
  parser.add_option_to_group("format", "--json", "-j", "JSON");
  parser.add_option_to_group("format", "--xml", "-x", "XML");

  // This should trigger validation error
  const char *argv[] = {"test", "--json", "--xml", nullptr};

  // Need to check that parse() calls help_message() or throws
  // This may require refactoring help_message to be testable
  REQUIRE_THROWS(parser.parse(argv)); // Or however errors are handled
}

TEST_CASE("Mutex groups - required group enforcement", "[mutex_groups]")
{
  TestArgParser parser;
  parser.add_mutex_group("format", true, "Output format (required)");
  parser.add_option_to_group("format", "--json", "-j", "JSON");

  // No format option provided - should error
  const char *argv[] = {"test", nullptr};

  REQUIRE_THROWS(parser.parse(argv)); // Or check error handling
}

TEST_CASE("Mutex groups - with subcommands", "[mutex_groups]")
{
  TestArgParser           parser;
  TestArgParser::Command &cmd = parser.add_command("drain", "Drain server");

  cmd.add_mutex_group("drain_mode", false, "Drain mode");
  cmd.add_option_to_group("drain_mode", "--no-new-connection", "-N", "...");
  cmd.add_option_to_group("drain_mode", "--undo", "-U", "...");

  const char   *argv[] = {"test", "drain", "--undo", nullptr};
  ts::Arguments args   = parser.parse(argv);

  REQUIRE(args.get("drain") == true);
  REQUIRE(args.get("undo") == true);
  REQUIRE(args.get("no-new-connection") == false);

  // multiple options in the same group
  const char *argv2[] = {"test", "drain", "--undo", "--no-new-connection", nullptr};

  REQUIRE_THROWS(parser.parse(argv2)); // Or check error handling
}

TEST_CASE("Mutex groups - error when group not created", "[mutex_groups]")
{
  TestArgParser parser;
  // Try to add option to a group that doesn't exist - should throw
  REQUIRE_THROWS(parser.add_option_to_group("nonexistent", "--test", "-t", "Test option"));
}
