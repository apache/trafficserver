/** @file

  A brief file description

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

#include "ts/ink_platform.h"
#include "ts/ink_args.h"
#include "ts/I_Version.h"
#include "ts/I_Layout.h"
#include "I_RecProcess.h"
#include "RecordsConfig.h"
#include "ts/runroot.h"
#include "engine.h"
#include "file_system.h"
#include "info.h"

#include <iostream>
#include <fstream>
#include <set>

using namespace std::literals;

struct subcommand {
  int (*handler)(int, const char **);
  const std::string name;
  const std::string help;
};

// Command line arguments (parsing)
struct CommandLineArgs {
  int layout;
  int features;
  int json;
};

static CommandLineArgs cl;

const ArgumentDescription argument_descriptions[] = {
  {"layout", 'l', "Show the layout (this is the default with no options given)", "T", &cl.layout, nullptr, nullptr},
  {"features", 'f', "Show the compiled features", "T", &cl.features, nullptr, nullptr},
  {"json", 'j', "Produce output in JSON format (when supported)", "T", &cl.json, nullptr, nullptr},
  VERSION_ARGUMENT_DESCRIPTION(),
  RUNROOT_ARGUMENT_DESCRIPTION()};

// the usage help message for subcommand
static void
help_usage()
{
  std::cout << "\nSubcommands:\n"
               "info         Show the layout as default\n"
               "init         Initialize the ts_runroot sandbox\n"
               "remove       Remove the ts_runroot sandbox\n"
               "verify       Verify the ts_runroot paths\n"
            << std::endl;
  std::cout << "Switches of runroot:\n"
               "--path:      Specify the path of the runroot\n"
               "--force:     Force to create or remove ts_runroot\n"
               "--absolute:  Produce absolute path in the yaml file during init\n"
               "--run-root(=/path):  Using specified TS_RUNROOT as sandbox\n"
               "--fix:       fix the premission issues that verify found"
            << std::endl;
  printf("Detailed usage and description in traffic_layout.en.rst\n");
  printf("\nGeneral Usage:\n");
  usage(argument_descriptions, countof(argument_descriptions), nullptr);
}

int
info(int argc, const char **argv)
{
  // take the "info" out from command line
  if (argv[1] && argv[1] == "info"sv) {
    for (int i = 1; i < argc; i++) {
      argv[i] = argv[i + 1];
    }
  }
  // detect help command
  int i = 1;
  while (argv[i]) {
    if (argv[i] == "--help"sv || argv[i] == "-h"sv) {
      help_usage();
    }
    ++i;
  }

  AppVersionInfo appVersionInfo;
  appVersionInfo.setup(PACKAGE_NAME, "traffic_layout", PACKAGE_VERSION, __DATE__, __TIME__, BUILD_MACHINE, BUILD_PERSON, "");
  // Process command line arguments and dump into variables
  if (!process_args_ex(&appVersionInfo, argument_descriptions, countof(argument_descriptions), argv) ||
      file_arguments[0] != nullptr) {
    help_usage();
  }

  runroot_handler(argv, 0 != cl.json);

  if (cl.features) {
    produce_features(0 != cl.json);
  } else {
    produce_layout(0 != cl.json);
  }
  return 0;
}

// handle everything with runroot using engine
int
traffic_runroot(int argc, const char **argv)
{
  // runroot engine for operations
  RunrootEngine engine;

  int i = 0;
  while (argv[i]) {
    engine._argv.push_back(argv[i]);
    ++i;
  }
  // parse the command line & put into global variable
  if (!engine.runroot_parse()) {
    engine.runroot_help_message(true, true, true);
    return 0;
  }
  // check sanity of the command about the runroot program
  engine.sanity_check();

  // create layout for runroot handling
  runroot_handler(argv);
  Layout::create();

  // check the command to execute
  if (engine.run_flag) {
    engine.create_runroot();
  } else if (engine.clean_flag) {
    engine.clean_runroot();
  } else if (engine.verify_flag) {
    engine.verify_runroot();
  }

  return 0;
}

int
main(int argc, const char **argv)
{
  const subcommand commands[] = {
    {info, "info", "Show the layout"},
    {traffic_runroot, "init", "Initialize the ts_runroot sandbox"},
    {traffic_runroot, "remove", "Remove the ts_runroot sandbox"},
    {traffic_runroot, "verify", "verify the ts_runroot paths"},
    {traffic_runroot, "fix", "fix permmision issue of the ts_runroot"},
  };

  // with command (info, init, remove)
  for (unsigned i = 0; i < countof(commands); ++i) {
    if (!argv[1]) {
      break;
    }
    if (strcmp(argv[1], commands[i].name.c_str()) == 0) {
      return commands[i].handler(argc, argv);
    }
  }

  // without command (info, init, remove), default behavior
  return info(argc, argv);
}
