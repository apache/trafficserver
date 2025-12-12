/** @file

  Configuration format conversion command for traffic_ctl.

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

#pragma once

#include "CtrlCommands.h"

/**
 * Command handler for configuration format conversion.
 *
 * Converts configuration files from legacy formats to YAML.
 * Supports: ssl_multicert
 */
class ConvertConfigCommand : public CtrlCommand
{
public:
  /**
   * Construct the command from parsed arguments.
   *
   * @param args Parsed command line arguments.
   */
  ConvertConfigCommand(ts::Arguments *args);

private:
  void convert_ssl_multicert();

  std::string _input_file;
  std::string _output_file;
};
