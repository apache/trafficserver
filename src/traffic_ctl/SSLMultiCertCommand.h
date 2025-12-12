/** @file

  SSL Multi-Certificate configuration command for traffic_ctl.

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
 * Command handler for ssl-multicert configuration operations.
 *
 * Supports reading and displaying ssl_multicert configuration in JSON or YAML format.
 */
class SSLMultiCertCommand : public CtrlCommand
{
public:
  /**
   * Construct the command from parsed arguments.
   *
   * @param args Parsed command line arguments.
   */
  SSLMultiCertCommand(ts::Arguments *args);

private:
  void show_config();
};
