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
 *
 *
 ****************************************************************/

#include "CliCreateCommands.h"
#include "createCommand.h"
#include "ShowCmd.h"
#include "ConfigCmd.h"
#include "UtilCmds.h"
#include "CliDisplay.h"
#include "I_Layout.h"
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <algorithm>

////////////////////////////////////////////////////////////////
// Called during Tcl_AppInit, this function creates the CLI commands
//
int
CliCreateCommands()
{
  createCommand("config:root", Cmd_ConfigRoot, NULL, CLI_COMMAND_EXTERNAL, "config:root", "Switch to root user");

  createCommand("show", Cmd_Show, NULL, CLI_COMMAND_EXTERNAL, "show", "Show command");

  createCommand("config", Cmd_Config, NULL, CLI_COMMAND_EXTERNAL, "config", "Config command");

  createCommand("show:status", Cmd_ShowStatus, NULL, CLI_COMMAND_EXTERNAL, "show:status", "Proxy status");

  createCommand("show:version", Cmd_ShowVersion, NULL, CLI_COMMAND_EXTERNAL, "show:version", "Version information");


  createCommand("show:security", Cmd_ShowSecurity, NULL, CLI_COMMAND_EXTERNAL, "show:security", "Security information");

  createCommand("show:http", Cmd_ShowHttp, NULL, CLI_COMMAND_EXTERNAL, "show:http", "HTTP protocol configuration");

  createCommand("show:icp", Cmd_ShowIcp, CmdArgs_ShowIcp, CLI_COMMAND_EXTERNAL,
                "show:icp [peer]", "ICP protocol configuration");

  createCommand("show:proxy", Cmd_ShowProxy, NULL, CLI_COMMAND_EXTERNAL, "show:proxy", "Proxy configuration");

  createCommand("show:cache", Cmd_ShowCache, CmdArgs_ShowCache, CLI_COMMAND_EXTERNAL,
                "show:cache [rules|storage]", "Cache configuration");

  createCommand("show:virtual-ip", Cmd_ShowVirtualIp, NULL, CLI_COMMAND_EXTERNAL,
                "show:virtual-ip", "Virtual-ip configuration");

  createCommand("show:hostdb", Cmd_ShowHostDb, NULL, CLI_COMMAND_EXTERNAL,
                "show:hostdb", "Host database configuration");

  createCommand("show:dns-resolver", Cmd_ShowDnsResolver, NULL, CLI_COMMAND_EXTERNAL,
                "show:dns-resolver", "DNS resolver configuration");

  createCommand("show:logging", Cmd_ShowLogging, NULL, CLI_COMMAND_EXTERNAL, "show:logging", "Logging configuration");

  createCommand("show:ssl", Cmd_ShowSsl, NULL, CLI_COMMAND_EXTERNAL, "show:ssl", "SSL configuration");

  createCommand("show:parent", Cmd_ShowParents, CmdArgs_ShowParents, CLI_COMMAND_EXTERNAL,
                "show:parent", "Parent configuration");

  createCommand("show:remap", Cmd_ShowRemap, NULL, CLI_COMMAND_EXTERNAL, "show:remap", "Remap configuration");

  createCommand("show:socks", Cmd_ShowSocks, CmdArgs_ShowSocks, CLI_COMMAND_EXTERNAL,
                "show:socks", "SOCKS configuration");

  createCommand("show:scheduled-update", Cmd_ShowScheduledUpdate, CmdArgs_ShowScheduledUpdate, CLI_COMMAND_EXTERNAL,
                "show:scheduled-update", "Scheduled update configuration");

  createCommand("show:proxy-stats", Cmd_ShowProxyStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:proxy-stats", "Proxy statistics");

  createCommand("show:http-trans-stats", Cmd_ShowHttpTransStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:http-trans-stats", "HTTP transaction statistics");

  createCommand("show:http-stats", Cmd_ShowHttpStats, NULL, CLI_COMMAND_EXTERNAL, "show:http-stats", "HTTP statistics");

  createCommand("show:icp-stats", Cmd_ShowIcpStats, NULL, CLI_COMMAND_EXTERNAL, "show:icp-stats", "ICP statistics");

  createCommand("show:cache-stats", Cmd_ShowCacheStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:cache-stats", "Cache statistics");

  createCommand("show:hostdb-stats", Cmd_ShowHostDbStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:hostdb-stats", "Host database statistics");

  createCommand("show:dns-stats", Cmd_ShowDnsStats, NULL, CLI_COMMAND_EXTERNAL, "show:dns-stats", "DNS statistics");

  createCommand("show:logging-stats", Cmd_ShowLoggingStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:logging-stats", "Logging statistics");

  createCommand("show:alarms", Cmd_ShowAlarms, NULL, CLI_COMMAND_EXTERNAL, "show:alarms", "Show active alarms");

  createCommand("show:cluster", Cmd_ShowCluster, NULL, CLI_COMMAND_EXTERNAL,
                "show:cluster", "Show Cluster Ports Settings");

  createCommand("config:get", Cmd_ConfigGet, NULL, CLI_COMMAND_EXTERNAL,
                "config:get <variable>", "Display a variable value");

  createCommand("config:set", Cmd_ConfigSet, NULL, CLI_COMMAND_EXTERNAL,
                "config:set <variable> <value>", "Set variable to specified value");

  createCommand("config:name", Cmd_ConfigName, NULL, CLI_COMMAND_EXTERNAL,
                "config:name <string>", "Set proxy name <string>");

  createCommand("config:start", Cmd_ConfigStart, NULL, CLI_COMMAND_EXTERNAL, "config:start", "Start proxy software");

  createCommand("config:stop", Cmd_ConfigStop, NULL, CLI_COMMAND_EXTERNAL, "config:stop", "Stop proxy software");

  createCommand("config:hard-restart", Cmd_ConfigHardRestart, NULL, CLI_COMMAND_EXTERNAL,
                "config:hard-restart", "Perform Hard Restart of all software components");

  createCommand("config:restart", Cmd_ConfigRestart, CmdArgs_ConfigRestart, CLI_COMMAND_EXTERNAL,
                "config:restart [cluster]", "Perform Restart of proxy software");

  createCommand("config:ssl", Cmd_ConfigSsl, CmdArgs_ConfigSsl, CLI_COMMAND_EXTERNAL,
                "config:ssl status <on | off>\n" "config:ssl ports <int>", "Configure ssl");

  createCommand("config:parent", Cmd_ConfigParents, CmdArgs_ConfigParents, CLI_COMMAND_EXTERNAL,
                "config:parent status <on | off>\n"
                "config:parent name <parent>\n" "config:parent rules <url>", "Update parent configuration");


  createCommand("config:remap", Cmd_ConfigRemap, NULL, CLI_COMMAND_EXTERNAL,
                "config:remap <url>", "Update remap configuration file <url>");

  createCommand("config:security", Cmd_ConfigSecurity, CmdArgs_ConfigSecurity, CLI_COMMAND_EXTERNAL,
                "config:security <ip-allow | mgmt-allow | admin> <url-config-file>\n"
                "config:security password", "Update security configuration");

  createCommand("config:http", Cmd_ConfigHttp, CmdArgs_ConfigHttp, CLI_COMMAND_EXTERNAL,
                "config:http status <on | off>\n"
                "config:http <keep-alive-timeout-in | keep-alive-timeout-out> <seconds>\n"
                "config:http <inactive-timeout-in | inactive-timeout-out> <seconds>\n"
                "config:http <active-timeout-in | active-timeout-out> <seconds>\n"
                "config:http <remove-from | remove-referer> <on | off>\n"
                "config:http <remove-user | remove-cookie> <on | off>\n"
                "config:http <remove-header> <string>\n"
                "config:http <insert-ip | remove-ip> <on | off>\n"
                "config:http proxy <fwd | rev | fwd-rev>", "Configure HTTP");

  createCommand("config:icp", Cmd_ConfigIcp, CmdArgs_ConfigIcp, CLI_COMMAND_EXTERNAL,
                "config:icp mode <disabled | receive | send-receive>\n"
                "config:icp port <int>\n"
                "config:icp multicast <on | off>\n"
                "config:icp query-timeout <seconds>\n" "config:icp peers <url-config-file>", "Configure ICP");

  createCommand("config:scheduled-update", Cmd_ConfigScheduledUpdate, CmdArgs_ConfigScheduledUpdate,
                CLI_COMMAND_EXTERNAL,
                "config:scheduled-update status <on | off>\n" "config:scheduled-update retry-count <int>\n"
                "config:scheduled-update retry-interval <sec>\n" "config:scheduled-update max-concurrent <int>\n"
                "config:scheduled-update force-immediate <on | off>\n"
                "config:scheduled-update rules <url-config-file>", "Configure Scheduled Update");

  createCommand("config:socks", Cmd_ConfigSocks, CmdArgs_ConfigSocks, CLI_COMMAND_EXTERNAL,
                "config:socks status <on | off>\n"
                "config:socks version <version>\n"
                "config:socks default-servers <string>\n"
                "config:socks accept <on | off>\n" "config:socks accept-port <int>", "Configure Socks");

  createCommand("config:cache", Cmd_ConfigCache, CmdArgs_ConfigCache, CLI_COMMAND_EXTERNAL,
                "config:cache <http> <on | off>\n"
                "config:cache ignore-bypass <on | off>\n"
                "config:cache <max-object-size | max-alternates> <int>\n"
                "config:cache file <url-config-file>\n"
                "config:cache freshness verify <when-expired | no-date | always | never>\n"
                "config:cache freshness minimum <explicit | last-modified | nothing>\n"
                "config:cache freshness no-expire-limit greater-than <sec> less-than <sec>\n"
                "config:cache <dynamic | alternates> <on | off>\n"
                "config:cache vary <text | images | other> <string>\n"
                "config:cache cookies <none | all | images | non-text>\n" "config:cache clear", "Configure Cache");


  createCommand("config:hostdb", Cmd_ConfigHostdb, CmdArgs_ConfigHostdb, CLI_COMMAND_EXTERNAL,
                "config:hostdb <lookup-timeout | foreground-timeout> <seconds>\n"
                "config:hostdb <background-timeout | invalid-host-timeout> <seconds>\n"
                "config:hostdb <re-dns-on-reload> <on | off>\n" "config:hostdb clear", "Configure Host Database");

  createCommand("config:logging", Cmd_ConfigLogging, CmdArgs_ConfigLogging, CLI_COMMAND_EXTERNAL,
                "config:logging event <enabled | trans-only | error-only | disabled>\n"
                "config:logging mgmt-directory <string>\n"
                "config:logging <space-limit | space-headroom> <megabytes>\n"
                "config:logging collation-status <inactive | host | send-standard |\n"
                "                                 send-custom | send-all>\n"
                "config:logging collation-host <string>\n"
                "config:logging collation secret <string> tagged <on | off> orphan-limit <int>\n"
                "config:logging format <squid | netscape-common | netscape-ext | netscape-ext2> <on | off>\n"
                "               type <ascii | binary> file <string> header <string>\n"
                "config:logging splitting <icp | http> <on | off>\n"
                "config:logging custom <on | off> format <traditional | xml>\n"
                "config:logging rolling <on | off> offset <hour> interval <hours>\n"
                "               auto-delete <on | off>", "Configure Logging");


  createCommand("config:dns", Cmd_ConfigDns, CmdArgs_ConfigDns, CLI_COMMAND_EXTERNAL,
                "config:dns resolve-timeout <seconds>\n" "config:dns retries <int>", "Configure DNS");

  createCommand("config:virtual-ip", Cmd_ConfigVirtualip, CmdArgs_ConfigVirtualip, CLI_COMMAND_EXTERNAL,
                "config:virtual-ip status <on | off>\n"
                "config:virtual-ip list\n"
                "config:virtual-ip add <x.x.x.x> device <string> sub-intf <int>\n"
                "config:virtual-ip delete <virtual ip number>", "Configure virtual-ip");

  createCommand("config:alarms", Cmd_ConfigAlarm, CmdArgs_ConfigAlarm, CLI_COMMAND_EXTERNAL,
                "config:alarms resolve-name <string>\n"
                "config:alarms resolve-number <int>\n"
                "config:alarms resolve-all\n"
                "config:alarms notify <on | off>", "Resolve Alarms, Turn notification on/off");

  createCommand("enable", Cmd_Enable, CmdArgs_Enable, CLI_COMMAND_EXTERNAL,
                "enable \n" "enable status ", "Enable Restricted Commands");

  createCommand("disable", Cmd_Disable, NULL, CLI_COMMAND_EXTERNAL, "disable", "Disable Restricted Commands");


  createCommand("debug", DebugCmd, DebugCmdArgs, CLI_COMMAND_EXTERNAL,
                "debug <on|off>", "Turn debugging print statements on/off");

  createCommand("help", Cmd_Help, NULL, CLI_COMMAND_EXTERNAL,
                "help [topic]", "Display online help");

  return CLI_OK;
}

#if defined(__SUNPRO_CC)

// Solaris doesn't like to link libstdc++ code. I don't have a system to test. Sorry.

int
Cmd_Help(ClientData, Tcl_Interp *, int, const char *[])
{
  return CMD_OK;
}

#else

struct replace_colon
{
  char operator() (char c) const {
    return (c == ':') ? '_' : c;
  }
};

static int
xsystem(const char * cmd)
{
  // Some versions of glibc declare system(3) with the warn_unused_result
  // attribute. Pretend to use the return value so it will shut the hell up.
  return system(cmd);
}

int
Cmd_Help(ClientData /* clientData ATS_UNUSED */, Tcl_Interp * /* interp ATS_UNUSED */, int argc, const char *argv[])
{
  Cli_Debug("looking for online help in %s\n", Layout::get()->datadir);

  for (int i = 1; i < argc; ++i) {
    std::ostringstream  cmd;
    std::string         topic(argv[i]);

    // Replace ':' with '_' so we can find the right on-disk man page.
    std::transform(topic.begin(), topic.end(), topic.begin(), replace_colon());

    // Check whether we have the man page on disk before we pass any user input
    // to the shell via system(3).
    cmd << Layout::get()->datadir << "/trafficshell/" << topic << ".1";
    if (access(cmd.str().c_str(), R_OK) != 0) {
      Cli_Debug("missing %s\n", cmd.str().c_str());
      continue;
    }

    cmd.clear();
    cmd.seekp(std::ios_base::beg);

    cmd << "man "
      << Layout::get()->datadir << "/trafficshell/" << topic << ".1";

    Cli_Debug("%s\n", cmd.str().c_str());
    xsystem(cmd.str().c_str());
  }

  return CMD_OK;
}

#endif  /* __SUNPRO_C */

// vim: set ts=2 sw=2 et :
