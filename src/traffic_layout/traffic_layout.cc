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

#include "tscore/I_Layout.h"
#include "tscore/runroot.h"
#include "engine.h"

using namespace std::literals;

int
main(int argc, const char **argv)
{
  LayoutEngine engine;

  int i = 0;
  while (argv[i]) {
    engine._argv.push_back(argv[i]);
    ++i;
  }
  engine.parser.add_global_usage("traffic_layout CMD [OPTIONS]");

  // global options
  engine.parser.add_option("--help", "-h", "Print usage information")
    .add_option("--run-root", "", "using TS_RUNROOT as sandbox", "", 1)
    .add_option("--version", "-V", "Print version string");

  // info command
  engine.parser.add_command("info", "Show the layout as default", [&]() { engine.info(); })
    .add_option("--features", "", "Show the compiled features")
    .add_option("--json", "-j", "Produce output in JSON format (when supported)")
    .set_default();
  // init command
  engine.parser.add_command("init", "Initialize(create) the runroot sandbox", [&]() { engine.create_runroot(); })
    .add_option("--absolute", "-a", "Produce absolute path in the runroot.yaml")
    .add_option("--force", "-f", "Create runroot even if the directory is not empty")
    .add_option("--path", "-p", "Specify the path of the runroot", "", 1)
    .add_option("--copy-style", "-c", "Specify the way of copying (full/hard/soft)", "", 1)
    .add_option("--layout", "-l", "Use specific layout (providing YAML file) to create runroot", "", 1);
  // remove command
  engine.parser.add_command("remove", "Remove the runroot sandbox", [&]() { engine.remove_runroot(); })
    .add_option("--force", "-f", "Remove runroot even if runroot.yaml is not found")
    .add_option("--path", "-p", "Specify the path of the runroot", "", 1);
  // verify command
  engine.parser.add_command("verify", "Verify the runroot permissions", [&]() { engine.verify_runroot(); })
    .add_option("--fix", "-x", "Fix the permission issues of runroot")
    .add_option("--path", "-p", "Specify the path of the runroot", "", 1)
    .add_option("--with-user", "-w", "verify runroot with certain user", "", 1);

  engine.arguments = engine.parser.parse(argv);

  runroot_handler(argv, engine.arguments.get("json"));
  Layout::create();

  engine.arguments.invoke();

  return 0;
}
