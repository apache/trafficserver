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

/****************************************************************
 * Filename: CliCreateCommands.cc
 * Purpose: This file contains the CLI command creation function.
 ****************************************************************/

#include <createCommand.h>
#include "ConfigCmd.h"
#include "ConfigUpgradeCmd.h"

////////////////////////////////////////////////////////////////
// Called during Tcl_AppInit, this function creates the CLI MIXT commands
//

int
CliCreateUpgradeCmd()
{

  // Fix INKqa12225: removed config:write, config:read, config:save-url since they
  //                 are not used by the OTW Upgrade feature
#if 0
  createCommand("config:write", Cmd_ConfigWrite, CmdArgs_ConfigWrite, CLI_COMMAND_EXTERNAL,
                "config:write ifc-head ts-version <ts_version> build-date <date> platform <platform> nodes <no_of_nodes>\n"
                "config:write feature <feature-string>\n"
                "config:write tar <tarfilelist>\n"
                "config:write tar-info <tarball-name> filelist <filelist>\n"
                "config:write tar-common <filelist>\n"
                "config:write bin-dir <sub-dir> filelist <dir-file-lists>\n"
                "config:write bin-group <filelist>\n"
                "config:write bin-common <filelist>\n"
                "config:write lib-dir <sub-dir> filelist <dir-file-lists>\n"
                "config:write lib-group <filelist>\n"
                "config:write lib-common <filelist>\n"
                "config:write config-dir <sub-dir> filelist <dir-file-filelists>\n"
                "config:write config-group <filelist>\n"
                "config:write config-common <filelist>\n" "config:write common-file <filelist>", "Write the ifc file");

  createCommand("config:read", Cmd_ConfigRead, CmdArgs_ConfigRead, CLI_COMMAND_EXTERNAL,
                "config:read ifc-head\n "
                "config:read feature \n"
                "config:read tar \n"
                "config:read tar-info \n"
                "config:read tar-common \n"
                "config:read bin-dir \n"
                "config:read bin-group \n"
                "config:read bin-common \n"
                "config:read lib-dir \n"
                "config:read lib-group \n"
                "config:read lib-common \n"
                "config:read config-dir \n"
                "config:read config-group \n"
                "config:read config-common \n" "config:read common-file ", "Read the ifc file");

  createCommand("config:save-url", Cmd_ConfigSaveUrl,
                CmdArgs_ConfigSaveUrl, CLI_COMMAND_EXTERNAL, NULL, "save the URL file");
#endif

  return CLI_OK;
}
