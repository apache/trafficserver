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

/***************************************/
/****************************************************************************
 *
 *  Module: Help facility for CLI
 *  
 * 
 ****************************************************************************/

#include "inktomi++.h"
#include "ink_platform.h"
#include "ink_unused.h" /* MAGIC_EDITING_TAG */

/* local includes */

#include "CLIeventHandler.h"
#include "Tokenizer.h"
#include "TextBuffer.h"
#include "WebMgmtUtils.h"
#include "FileManager.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "CliUtils.h"
#include "CLI.h"

// Help usage at cli-> level

const int numCmd = 12;
static const char *HelpStrings[numCmd] = {
  "1.  monitor           # monitor mode \n",
  "2.  configure         # configure mode \n",
  "3.  reread            # forces a reread of the configuration files\n",
  "4.  shutdown          # Shuts down the traffic_server\n",
  "5.  startup           # Starts the traffic_server (local node)\n",
  "6.  bounce_local      # Restarts the traffic_server (local node) \n",
  "7.  bounce_cluster    # Restarts the traffic_server (cluster wide)\n",
  "8.  restart_local     # Restarts the traffic_manager (local node)\n",
  "9.  restart_cluster   # Restarts the traffic_manager (cluster wide)\n",
#if 0                           /* Don't allow clearing of statistics */
  "10. clear_cluster     # Clears Statistics (cluster wide)\n",
  "11. clear_node        # Clears Statistics (local node)\n",
#endif
  "     Select above options by number \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->monitor level
const int numCmdMonitor = 9;
static const char *MonitorHelpStrings[numCmdMonitor] = {
  "1. dashboard          # Dashboard level \n",
  "2. node               # Node level \n",
  "3. protocols          # Protocols level \n",
  "4. cache              # Cache level \n",
  "5. other              # Other level\n",
  "     Select above options by number \n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->monitor->dashboard level
const int numCmdMonDashboard = 7;
static const char *MonDashHelpStrings[numCmdMonDashboard] = {
  "1. show               # displays dashboard \n",
  "     Select above options by number \n",
  "alarms                # displays list of alarms \n",
  "resolve <alarm ID>    # resolve alarm <alarm ID> \n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->monitor->node level
const int numCmdMonNode = 9;
static const char *MonNodeHelpStrings[numCmdMonNode] = {
  "1. stats              # displays all the node statistics \n",
  "2. cache              # displays the node cache statistics \n",
  "3. inprogress         # displays the node in progress statistics \n",
  "4. network            # displays the node network statistics \n",
  "5. nameres            # displays the node name resolution statistics \n",
  "     Select above options by number \n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->monitor->protocols level
const int numCmdMonProtocols = 7;
static const char *MonProtHelpStrings[numCmdMonProtocols] = {
  "1. stats              # displays all the protocol statistics \n",
  "2. http               # displays the HTTP protocol statistics \n",
  "4. icp                # displays the ICP protocol statistics \n",
  "     Select above options by number \n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->monitor->cache level
const int numCmdMonCache = 5;
static const char *MonCacheHelpStrings[numCmdMonCache] = {
  "1. stats              # displays all the cache statistics \n",
  "     Select above options by number \n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->monitor->other level
const int numCmdMonOther = 10;
static const char *MonOtherHelpStrings[numCmdMonOther] = {
  "1. stats              # displays all the other statistics \n",
  "2. hostdb             # displays the host data base statistics \n",
  "3. dns                # displays the DNS statistics \n",
  "4. cluster            # displays the cluster statistics \n",
  "5. socks              # displays the SOCKS statistics \n",
  "6. logging            # displays the logging statistics \n",
  "     Select above options by number \n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure level
//
// Punt for now on more robust display of options for users
// to select. This would entail adding more functionality
// that comes for free if you use 'curses'.
//
// One possiblity is for ascii based use isto use 'lynx' WWW to access 
// the web configuration
//

const int numCmdConfigure = 13;
/*   "7. snapshots          # Snapshots configuration level\n",  */
static const char *ConfigureHelpStrings[numCmdConfigure] = {
  "1. server             # Server configuration level \n",
  "2. protocols          # Protocols configuration level\n",
  "3. cache              # Cache configuration level\n",
  "4. security           # Security configuration level\n",
  "5. logging            # Logging configuration level\n",
  "6. routing            # Routing configuration level\n",
  "7. hostdb             # Host Database configuration level\n",
  "     Select above options by number \n",
  "set <var> <value>     # sets var to value\n",
  "get <var>             # gets value of var \n",
  ".                     # Move back to previous level\n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->server level
const int numCmdConfServer = 13;
static const char *ConfigureServerHelpStrings[numCmdConfServer] = {
  "1. display            # displays all configuration variables \n",
  "2. server             # server configuration variables \n",
  "3. web management     # web management configuration variables \n",
  "4. virtual-ip         # virtual ip configuration variables \n",
  "5. auto-configuration # auto configuration variables \n",
  "6. throttling         # server throttling configuration variables \n",
  "7. SNMP               # SNMP configuration variables \n",
  "8. Customizable Response Pages # \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->protocols level
const int numCmdConfProtocols = 7;
static const char *ConfigureProtHelpStrings[numCmdConfProtocols] = {
  "1. display            # displays all configuration variables \n",
  "2. http               # HTTP configuration variables \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->cache level
const int numCmdConfCache = 10;
static const char *ConfigureCacheHelpStrings[numCmdConfCache] = {
  "1. display            # displays all configuration variables \n",
  "2. cache storage      # cache storage configuration variables \n",
  "3. cache activation   # cache activation configuration variables \n",
  "4. cache freshness    # cache freshness configuration variables \n",
  "5. cache content      # cache variable content configuration variables \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->security level
const int numCmdConfSecurity = 8;
static const char *ConfigureSecHelpStrings[numCmdConfSecurity] = {
  "1. display            # displays all configuration variables \n",
  "2. access             # access configuration variables \n",
  "3. firewall           # firewall configuration variables \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->routing level
const int numCmdConfRouting = 9;
static const char *ConfigureRoutHelpStrings[numCmdConfRouting] = {
  "1. display            # displays all configuration variables \n",
  "2. parent proxy       # Parent Proxy configuration variables \n",
  "3. ICP                # ICP configuration variables \n",
  "4. reverse proxy      # reverse proxy configuration variables \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->hostdb level
const int numCmdConfHostdb = 8;
static const char *ConfigureHostdbHelpStrings[numCmdConfHostdb] = {
  "1. display            # displays all configuration variables \n",
  "2. host database      # host database configuration variables \n",
  "3. DNS                # DNS configuration variables \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->logging level
const int numCmdConfLogging = 14;
static const char *ConfigureLogHelpStrings[numCmdConfLogging] = {
  "1. display            # displays all configuration variables \n",
  "2. event logging      # event logging configuration variables \n",
  "3. log managment      # log management configuration variables \n",
  "4. log collation      # log collation configuration variables \n",
  "5. Squid format       # Squid format configuration variables \n",
  "6. Netscape Common    # Netscape Common format configuration variables \n",
  "7. Netscape Extended  # Netscape Extended format configuration variables \n",
  "8. Netscape Extended2 # Netscape Extended2 format configuration variables \n",
  "9. Log Rolling/Splitting # Log Rolling and Splitting configuration variables \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

// Help usage at cli->configure->snapshots level
const int numCmdConfSnapshots = 6;
static const char *ConfigureSnapHelpStrings[numCmdConfSnapshots] = {
  "1. display            # displays all configuration variables \n",
  "     Select above options by number \n",
  "change <no> <value>   # sets variable shown by <no> to <value>\n",
  ".                     # Move back to previous level \n",
  "help                  # displays a list of commands\n",
  "exit                  # exits the cmd line tool\n"
};

//
// Print help usage at appropriate interactive level
//
void
CLI_globals::Help(textBuffer * output,  /* IN/OUT: output buffer */
                  cmdline_states hlevel,        /*     IN: command level */
                  int advui,    /*     IN: */
                  int featset /*     IN: */ )
{
  int i;

  // Prepare help response header
  output->copyFrom(successStr, strlen(successStr));
  CLI_globals::set_prompt(output, hlevel);

  Debug("cli", "help: advui(%d),featset(%d), hlevel(%d)\n", advui, featset, hlevel);

  // create appropriate Help response
  // 
  // See 'MgmtFeat.cc'
  // advui == 1(10) -> Advanced UI only           (Regular TS HTTP)
  //       == 0(00) -> Simple UI only             (on hold....)
  //       == 2(01) -> RNI UI only                (TS for Real Networks, only does RNI caching)
  //       == 3(11) -> Advanced UI + RNI features (TS w/ RN support HTTP/RNI )
  // Currently only used to implement cheesy licensing
  // featset == 1 -> indicates that Advanced UI is enabled 
  //         == 0 -> indicates that Advanced UI is disabled
  switch (hlevel) {
  case CL_MONITOR:
    for (i = 0; i < numCmdMonitor; i++) {
      if ((0 == advui || 2 == advui) && (1 == i || 3 == i))
        continue;               // only show dashboard/protocols/other 
      // for simple/RNI UI
      output->copyFrom(MonitorHelpStrings[i], strlen(MonitorHelpStrings[i]));
    }
    break;
  case CL_MON_PROTOCOLS:
    for (i = 0; i < numCmdMonProtocols; i++) {
      if ((0 == advui || 2 == advui) && (1 == i || 2 == i || 4 == i))
        continue;               // only show 
      // for simple/RNI UI
      output->copyFrom(MonProtHelpStrings[i], strlen(MonProtHelpStrings[i]));
    }
    break;
  case CL_CONFIGURE:
    for (i = 0; i < numCmdConfigure; i++) {
      if ((0 == advui || 2 == advui) && 6 == i)
        continue;               // skip HostDB for simple/RNI UI
      else if (2 == advui && 5 == i)
        continue;               // skip routing for RNI UI
      output->copyFrom(ConfigureHelpStrings[i], strlen(ConfigureHelpStrings[i]));
    }
    break;
  case CL_MON_DASHBOARD:
    for (i = 0; i < numCmdMonDashboard; i++)
      output->copyFrom(MonDashHelpStrings[i], strlen(MonDashHelpStrings[i]));
    break;
  case CL_MON_NODE:
    for (i = 0; i < numCmdMonNode; i++)
      output->copyFrom(MonNodeHelpStrings[i], strlen(MonNodeHelpStrings[i]));
    break;
  case CL_MON_CACHE:
    for (i = 0; i < numCmdMonCache; i++)
      output->copyFrom(MonCacheHelpStrings[i], strlen(MonCacheHelpStrings[i]));
    break;
  case CL_MON_OTHER:
    for (i = 0; i < numCmdMonOther; i++) {
      if ((0 == advui || 2 == advui) && (3 == i || 4 == i))
        continue;               // only show hostdb/dns/logging
      // for simple/RNI UI
      output->copyFrom(MonOtherHelpStrings[i], strlen(MonOtherHelpStrings[i]));
    }
    break;
  case CL_CONF_SERVER:
    for (i = 0; i < numCmdConfServer; i++)
      output->copyFrom(ConfigureServerHelpStrings[i], strlen(ConfigureServerHelpStrings[i]));
    break;
  case CL_CONF_PROTOCOLS:
    for (i = 0; i < numCmdConfProtocols; i++)
      output->copyFrom(ConfigureProtHelpStrings[i], strlen(ConfigureProtHelpStrings[i]));
    break;
  case CL_CONF_CACHE:
    for (i = 0; i < numCmdConfCache; i++)
      output->copyFrom(ConfigureCacheHelpStrings[i], strlen(ConfigureCacheHelpStrings[i]));
    break;
  case CL_CONF_SECURITY:
    for (i = 0; i < numCmdConfSecurity; i++)
      output->copyFrom(ConfigureSecHelpStrings[i], strlen(ConfigureSecHelpStrings[i]));
    break;
  case CL_CONF_HOSTDB:
    for (i = 0; i < numCmdConfHostdb; i++)
      output->copyFrom(ConfigureHostdbHelpStrings[i], strlen(ConfigureHostdbHelpStrings[i]));
    break;
  case CL_CONF_LOGGING:
    for (i = 0; i < numCmdConfLogging; i++)
      output->copyFrom(ConfigureLogHelpStrings[i], strlen(ConfigureLogHelpStrings[i]));
    break;
  case CL_CONF_SNAPSHOTS:
    for (i = 0; i < numCmdConfSnapshots; i++)
      output->copyFrom(ConfigureSnapHelpStrings[i], strlen(ConfigureSnapHelpStrings[i]));
    break;
  case CL_CONF_ROUTING:
    for (i = 0; i < numCmdConfRouting; i++)
      output->copyFrom(ConfigureRoutHelpStrings[i], strlen(ConfigureRoutHelpStrings[i]));
    break;
  case CL_BASE:
  default:
    for (i = 0; i < numCmd; i++)
      output->copyFrom(HelpStrings[i], strlen(HelpStrings[i]));
    break;
  }                             //end switch

}                               // end Help()
