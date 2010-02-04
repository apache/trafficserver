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
 *  CLI.cc - code to handle server side command line interface
 *  
 * 
 ****************************************************************************/

#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

#include "ink_platform.h"
#include "inktomi++.h"
#include "Main.h"
#include "Tokenizer.h"
#include "TextBuffer.h"
#include "WebMgmtUtils.h"
#include "FileManager.h"
#include "MgmtUtils.h"
#include "CliUtils.h"
#include "CLI.h"
#include "MgmtServerRPC.h"

#include "FSM.h"                /* Finite State Machine */
#include "CLIeventHandler.h"

#include "CLIlineBuffer.h"

#include "P_RecCore.h"

#define MAX_BUF_READ_SIZE 1024

/* Protocol strings
 *        
 *  Our protocol is that tranmissions always end
 *    with a null character
 *
 *  Replys from the server have 
 *     "1;" prefixed on success and
 *     "0;" prefixed on failure. 
 *
 *   The client is responsible for stripping off the
 *     success/fail prefix
 */

/* Initialzation of some global strings */
const char *
  CLI_globals::successStr = "1;";
const char *
  CLI_globals::failStr = "0;";
const char *
  CLI_globals::unknownCmd = "Unknown command";

/* Initalization of global miscellaneous error strings */
const char *
  CLI_globals::argNum = "Invalid Number of Arguments";
const char *
  CLI_globals::varNotFound = "Variable Not Found";

const char *
  CLI_globals::sep1 = "--------------------------------------" "--------------------------------------\n";
const char *
  CLI_globals::sep2 = "**************************************" "**************************************\n";

// Initialization of global Table of command levels with their prompts
const
  CLI_globals::CLI_LevelDesc
  CLI_globals::cmdLD[] = {
  {CL_BASE, "cli->;"},
  {CL_MONITOR, "monitor->;"},
  {CL_CONFIGURE, "configure->;"},
  {CL_MON_DASHBOARD, "dashboard->;"},
  {CL_MON_NODE, "node->;"},
  {CL_MON_PROTOCOLS, "protocols->;"},
  {CL_MON_CACHE, "cache->;"},
  {CL_MON_OTHER, "other->;"},
  {CL_CONF_SERVER, "server->;"},
  {CL_CONF_PROTOCOLS, "protocols->;"},
  {CL_CONF_CACHE, "cache->;"},
  {CL_CONF_SECURITY, "security->;"},
  {CL_CONF_HOSTDB, "hostdb->;"},
  {CL_CONF_LOGGING, "logging->;"},
  {CL_CONF_SNAPSHOTS, "snapshots->"},
  {CL_CONF_ROUTING, "routing->;"}
};

//
// Set the prompt to show at the command line
// for the given command level
//
void
CLI_globals::set_prompt(textBuffer * output,    /* IN/OUT: output buffer */
                        cmdline_states plevel /*     IN: command level */ )
{
  output->copyFrom(CLI_globals::cmdLD[plevel].cmdprompt, strlen(CLI_globals::cmdLD[plevel].cmdprompt));
  //Debug("cli_prompt","set_prompt: new prompt %s\n", CLI_globals::cmdLD[plevel].cmdprompt);
}                               // end set_prompt(...)

//
// Set the response string for the given level
//
void
CLI_globals::set_response(textBuffer * output,  /* IN/OUT: output buffer */
                          const char *header,   /*     IN: header */
                          const char *trailer,  /*     IN: trailer */
                          cmdline_states plevel /*     IN: command level */ )
{
  if (output && header && trailer) {
    output->copyFrom(header, strlen(header));
    set_prompt(output, plevel);
    output->copyFrom(trailer, strlen(trailer));
  }
}                               // end set_response(...)

void
CLI_globals::Get(char *largs,   /*     IN: arguments */
                 textBuffer * output,   /* IN/OUT: output buffer */
                 cmdline_states plevel /*     IN: command level */ )
{
  char buf[1024] = "";
  Tokenizer argTok(" ");

  if (largs == NULL) {
    CLI_globals::set_response(output, CLI_globals::failStr, CLI_globals::argNum, plevel);
  } else {
    argTok.Initialize(largs, SHARE_TOKS);
    if (varStrFromName(argTok[0], buf, 1024) == true) {
      CLI_globals::set_response(output, CLI_globals::successStr, buf, plevel);
    } else {
      CLI_globals::set_response(output, CLI_globals::failStr, CLI_globals::varNotFound, plevel);
    }
  }
}                               // end Get()


void
CLI_globals::Set(char *largs,   /*     IN: arguments */
                 textBuffer * output,   /* IN/OUT: output buffer */
                 cmdline_states plevel /*     IN: command level */ )
{
  const char setFailed[] = "Set Failed";
  const char setOK[] = "SetOK";
  const char configOnly[] = "Only configuration vars can be set";
  const char configVar[] = "proxy.config.";
  const char localVar[] = "proxy.local.";
  Tokenizer argTok(" ");
  char *recVal;

  if (largs == NULL || argTok.Initialize(largs) < 2) {
    // no args given
    CLI_globals::set_response(output, CLI_globals::failStr, CLI_globals::argNum, plevel);

  } else {                      // check proxy config variable
    if (strncmp(configVar, argTok[0], strlen(configVar)) == 0 || strncmp(localVar, argTok[0], strlen(localVar)) == 0) {
      // BZ48638
      recVal = largs + strlen(argTok[0]) + 1;

      if (varSetFromStr(argTok[0], recVal) == true) {
        CLI_globals::set_response(output, CLI_globals::successStr, setOK, plevel);
      } else {
        CLI_globals::set_response(output, CLI_globals::failStr, setFailed, plevel);
      }
    } else {                    // not proxy config variable
      CLI_globals::set_response(output, CLI_globals::failStr, configOnly, plevel);
    }
  }
}                               // end Set()

void
CLI_globals::Change(char *largs,        /*     IN: arguments */
                    const VarNameDesc * desctable,      /*     IN: description table */
                    int ndesctable,     /*     IN: number of descs */
                    textBuffer * output,        /* IN/OUT: output buffer */
                    cmdline_states plevel /*     IN: command level */ )
{
  const char adminpasswdVar[] = "proxy.config.admin.admin_password";
  const char guestpasswdVar[] = "proxy.config.admin.guest_password";
  const char setFailed[] = "Set Failed";
  const char setOK[] = "SetOK";
  const char configOnly[] = "Only configuration vars can be set";
  const char invalidNum[] = "Invalid number";
  const char configVar[] = "proxy.config.";
  const char localVar[] = "proxy.local.";
  int index = -1;
  Tokenizer argTok(" ");

  if (largs == NULL || argTok.Initialize(largs, SHARE_TOKS) != 2) {
    // no args given
    CLI_globals::set_response(output, CLI_globals::failStr, CLI_globals::argNum, plevel);
  } else {
    errno = 0;
    index = atoi(argTok[0]);
    if (0 == index && errno) {
      index = -1;
    }
    if (index >= 0 && index < ndesctable) {
      // translate number -> index in desctable[]
      // and check if proxy config variable
      if (strncmp(configVar, desctable[index].name, strlen(configVar)) == 0
          || strncmp(localVar, desctable[index].name, strlen(localVar)) == 0) {
        // yes
        // Check if changing admin/guest Passwords
        const char *config_value = argTok[1];
        char newpass1_md5_str[33] = ""; // used later to store md5 of password
        if (strncmp(adminpasswdVar, desctable[index].name, strlen(adminpasswdVar)) == 0
            || strncmp(guestpasswdVar, desctable[index].name, strlen(guestpasswdVar)) == 0) {
          // changing passwords
          const char *newpass1 = argTok[1];
          INK_DIGEST_CTX context;
          char newpass1_md5[16];

          ink_code_incr_md5_init(&context);
          ink_code_incr_md5_update(&context, newpass1, strlen(newpass1));
          ink_code_incr_md5_final(newpass1_md5, &context);
          ink_code_md5_stringify(newpass1_md5_str, sizeof(newpass1_md5_str), newpass1_md5);
          newpass1_md5_str[23] = '\0';  /* only use 23 characters */
          config_value = newpass1_md5_str;      // encrypted passord
        }

        Debug("cli", "CLI_globals::Change config=%s, value=%s \n", desctable[index].name, config_value);
        if (varSetFromStr(desctable[index].name, config_value) == true) {
          CLI_globals::set_response(output, CLI_globals::successStr, setOK, plevel);
        } else {
          CLI_globals::set_response(output, CLI_globals::failStr, setFailed, plevel);
        }
      } else {                  // not proxy config variable
        CLI_globals::set_response(output, CLI_globals::failStr, configOnly, plevel);
      }
    } else {
      CLI_globals::set_response(output, CLI_globals::failStr, invalidNum, plevel);
    }
  }
}                               // end Change()

void
CLI_globals::ReRead(char *largs,        /*     IN: arguments */
                    textBuffer * output,        /* IN/OUT: output buffer */
                    cmdline_states plevel /*     IN: command level */ )
{
  const char badArgs[] = "reread: Unknown Argument";
  const char ok[] = "configuration reread";

  if (largs == NULL) {
    // re-read configuration file
    configFiles->rereadConfig();
    lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, "*");
    CLI_globals::set_response(output, CLI_globals::successStr, ok, plevel);
  } else {
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  }
}                               // end ReRead()

void
CLI_globals::Shutdown(char *largs,      /*     IN: arguments */
                      textBuffer * output,      /* IN/OUT: output buffer */
                      cmdline_states plevel /*     IN: command level */ )
{
  const char badArgs[] = "shutdown: Unknown Argument";
  const char failed[] = "shutdown of traffic server failed";
  const char alreadyDown[] = "traffic_server is already off";
  const char ok[] = "traffic_server shutdown";

  if (largs != NULL) {
    // args
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  } else {                      // no args
    if (lmgmt->proxy_running == 0) {
      CLI_globals::set_response(output, CLI_globals::failStr, alreadyDown, plevel);
    } else {                    // proxy running
      if (ProxyShutdown() == true) {    // successfull shutdown
        CLI_globals::set_response(output, CLI_globals::successStr, ok, plevel);
      } else {
        CLI_globals::set_response(output, CLI_globals::failStr, failed, plevel);
      }
    }
  }
}                               // end Shutdown()

//
//    Enqueue an event to restart the proxies across
//      the cluster
//
void
CLI_globals::BounceProxies(char *largs, /*     IN: arguments */
                           textBuffer * output, /* IN/OUT: output buffer */
                           cmdline_states plevel /*     IN: command level */ )
{
  const char badArgs[] = "bounce: Unknown Argument";
  const char ok[] = "traffic_server bounce initiated";

  if (largs != NULL) {          // args
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  } else {                      // no args
    CLI_globals::set_response(output, CLI_globals::successStr, ok, plevel);
    // bounce cluster proxies
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_BOUNCE_PROCESS);
  }
}                               // end BounceProxies()

//
//    Restart the Local Proxy
//
void
CLI_globals::BounceLocal(char *largs,   /*     IN: arguments */
                         textBuffer * output,   /* IN/OUT: output buffer */
                         cmdline_states plevel /*     IN: command level */ )
{
  const char badArgs[] = "bounce: Unknown Argument";
  const char ok[] = "traffic_server bounce initiated";

  if (largs != NULL) {          // args
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  } else {                      // no args
    CLI_globals::set_response(output, CLI_globals::successStr, ok, plevel);
    // bounce the proxy
    lmgmt->processBounce();
  }
}                               // end BounceLocal()

//
//    Clears statistics.  If the cluster is set to true
//      stats are cleared cluster wide.  Other wise just
//      the local node is cleared
//

void
CLI_globals::ClearStats(char *largs,    /*     IN: arguments */
                        textBuffer * output,    /* IN/OUT: output buffer */
                        bool cluster,   /*     IN: clustering? */
                        cmdline_states plevel /*     IN: command level */ )
{
  const char result[] = "Statistics cleared";
  const char badArgs[] = "clear: Unknown argument";

  if (largs != NULL) {          // args
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  } else {                      // no args
    if (cluster == true) {
      lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_CLEAR_STATS); // cluster stats
    } else {
      lmgmt->clearStats();      // local stats
    }

    CLI_globals::set_response(output, CLI_globals::successStr, result, plevel);
  }
}                               // end cliClearStats()

//
//   A debugging aid.  Signals an alarm that we
//     can use for testing and debugging
//
void
CLI_globals::TestAlarm(textBuffer * output,     /* IN/OUT: output buffer */
                       cmdline_states plevel /*     IN: command level */ )
{
  const char result[] = "Test Alarm Generated";

  // send test alarm?
  lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_MGMT_TEST, "[LocalManager] Test Alarm");
  CLI_globals::set_response(output, CLI_globals::successStr, result, plevel);
}                               // end cliTestAlarm()

// 
// OEM_ALARM
// additional feature; allows the addition of a customized alarm to be 
// added from the command line ( -a option)
//
void
CLI_globals::AddAlarm(char *largs,      /* IN; arguments */
                      textBuffer * output,      /* IN/OUT: output buffer */
                      cmdline_states plevel /*   IN: command level */ )
{
  const char
    result[] = "OEM Alarm Generated";
  const char
    noresult[] = "No OEM Alarm text";
  if (largs == NULL) {
    // no args given
    CLI_globals::set_response(output, CLI_globals::failStr, noresult, plevel);
  } else {
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_ADD_ALARM, largs);
    CLI_globals::set_response(output, CLI_globals::successStr, result, plevel);
  }
}                               // end AddAlarm()

//   Initiate a shutdown of local manager  - local node only
//
//   Note: for the user this function is executed to do a manager 
//         restart.  The watcher will immediately restart the manager
//         so this function appears to "restart" the manger.  From
//         manager's perspective, all this code does is prepare
//         for shutdown
//
void
CLI_globals::ShutdownMgmtL(char *largs, /*     IN: arguments */
                           textBuffer * output, /* IN/OUT: output buffer */
                           cmdline_states plevel /*     IN: command level */ )
{
  const char badArgs[] = "restart_local: Unknown Argument";
  const char ok[] = "traffic_manager restart initiated";

  if (largs != NULL) {          // args
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  } else {                      // no args
    CLI_globals::set_response(output, CLI_globals::successStr, ok, plevel);
    lmgmt->mgmt_shutdown_outstanding = true;    // shutdown is true
  }
}                               // end cliShutdownMgmtL()

//    Initiate a shutdown of local manager  - cluster wide
//
//   Note: for the user this function is executed to do a manager 
//         restart.  The watcher will immediately restart the manager
//         so this function appears to "restart" the manger.  From
//         manager's perspective, all this code does is prepare
//         for shutdown
//
void
CLI_globals::ShutdownMgmtC(char *largs, /*     IN: arguments */
                           textBuffer * output, /* IN/OUT: output buffer */
                           cmdline_states plevel /*     IN: command level */ )
{
  const char badArgs[] = "restart_cluster: Unknown Argument";
  const char ok[] = "traffic_manager restart initiated";

  if (largs != NULL) {          // args
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  } else {                      // no args
    CLI_globals::set_response(output, CLI_globals::successStr, ok, plevel);
    // send cluster shutdown message
    lmgmt->ccom->sendClusterMessage(CLUSTER_MSG_SHUTDOWN_MANAGER);
  }
}                               // end cliShutdownMgmtC()

void
CLI_globals::Startup(char *largs,       /*     IN: arguments */
                     textBuffer * output,       /* IN/OUT: output buffer */
                     cmdline_states plevel /*     IN: command level */ )
{
  const char badArgs[] = "startup: Unknown Argument";
  const char failed[] = "startup of traffic server failed";
  const char alreadyUp[] = "traffic_server is already running";
  const char ok[] = "traffic_server started";
  int i = 0;

  if (largs != NULL) {          // args
    CLI_globals::set_response(output, CLI_globals::failStr, badArgs, plevel);
  } else {                      // no args
    // If we are already running, just note it
    if (lmgmt->proxy_running == 1) {    // already running
      CLI_globals::set_response(output, CLI_globals::failStr, alreadyUp, plevel);
      return;
    }
  }

  lmgmt->run_proxy = true;
  lmgmt->listenForProxy();

  // Wait for up to ten seconds for the proxy power up
  do {
    mgmt_sleep_sec(1);
  } while (i++ < 10 && lmgmt->proxy_running == 0);

  // Check to see if we made it back up
  if (lmgmt->proxy_running == 1) {
    CLI_globals::set_response(output, CLI_globals::successStr, ok, plevel);
  } else {
    CLI_globals::set_response(output, CLI_globals::failStr, failed, plevel);
  }

}                               // end Startup()

// 
// Used for congestion control feature. Returns a list of congested servers.
// 
void
CLI_globals::QueryDeadhosts(char *largs,        /*     IN: arguments */
                            textBuffer * output,        /* IN/OUT: output buffer */
                            cmdline_states plevel /*     IN: command level */ )
{
  int fd = send_cli_congest_request("list");
  if (fd < 0) {                 // error
    CLI_globals::set_response(output, CLI_globals::failStr, "query for congested servers failed", plevel);
  } else {
    output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
    set_prompt(output, plevel);

    int nread;
    char response[MAX_BUF_READ_SIZE];
    memset(response, 0, MAX_BUF_READ_SIZE);
    while ((nread = ink_read_socket(fd, response, MAX_BUF_READ_SIZE)) > 0) {
      output->copyFrom(response, nread);
      if (nread < MAX_BUF_READ_SIZE)
        break;
      memset(response, 0, MAX_BUF_READ_SIZE);
    }
    if (send_exit_request(fd) < 0) {    // also closes the fd 
      Debug("cli", "[QueryDeadhosts] error closing RAF connection");
    }
  }
}

const int MaxNumTransitions = 367;      // Maximum number of transitions in table

//
// handle cli connections
// NOTE: May need to change this when handling say a connect through
//       a telnet session to a port on the manager.
//
void
handleCLI(int cliFD,            /* IN: UNIX domain socket descriptor */
          WebContext * pContext /* IN: */ )
{
  char inputBuf[1025];
  int readResult;
  textBuffer input(1024);
  textBuffer output(1024);
  Tokenizer cmdTok(" ");
  CLI_DATA cli_data = { NULL, NULL, NULL, NULL, NULL, CL_EV_HELP, 0, 0 };
  cmdline_events event = CL_EV_ERROR;

  // An instance of a command line events handler 
  CmdLine_EventHandler evHandler(MaxNumTransitions);

  // Create an instance of a FSM for command line event handler
  FSM cliFSM(&evHandler, MaxNumTransitions, CL_BASE);

  // Define the FSM's transitions
  // Note all events have to be handled at each level since
  // users can input them anywhere by mistake. This makes
  // for a *huge* transition table but since we use a FSM there is
  // no easy way around this for now. 


  // base level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_BASE, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_ERROR, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_HELP, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_EXIT, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_PREV, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_GET, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_SET, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_DISPLAY, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_CHANGE, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_ADD_ALARM, Ind_BaseLevel);    // OEM_ALARM
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_QUERY_DEADHOSTS, Ind_BaseLevel);
  // events tied to number selection - 11
  cliFSM.defineTransition(CL_BASE, CL_MONITOR, CL_EV_ONE, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_CONFIGURE, CL_EV_TWO, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_THREE, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_FOUR, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_FIVE, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_SIX, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_SEVEN, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_EIGHT, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_NINE, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_TEN, Ind_BaseLevel);
  cliFSM.defineTransition(CL_BASE, CL_BASE, CL_EV_ELEVEN, Ind_BaseLevel);

  // Monitor Level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_MONITOR, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_ERROR, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_HELP, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_EXIT, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_BASE, CL_EV_PREV, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_GET, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_SET, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_DISPLAY, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_CHANGE, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_ADD_ALARM, Ind_MonitorLevel);   // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_MONITOR, CL_MON_DASHBOARD, CL_EV_ONE, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MON_NODE, CL_EV_TWO, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MON_PROTOCOLS, CL_EV_THREE, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MON_CACHE, CL_EV_FOUR, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MON_OTHER, CL_EV_FIVE, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_SIX, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_SEVEN, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_EIGHT, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_NINE, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_TEN, Ind_MonitorLevel);
  cliFSM.defineTransition(CL_MONITOR, CL_MONITOR, CL_EV_ELEVEN, Ind_MonitorLevel);

  // Monitor->Dashboard Level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_ERROR, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_HELP, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_EXIT, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MONITOR, CL_EV_PREV, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_GET, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_SET, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_DISPLAY, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_CHANGE, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_ADD_ALARM, Ind_MonitorDashboardLevel);      //OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_ONE, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_TWO, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_THREE, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_FOUR, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_FIVE, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_SIX, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_SEVEN, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_EIGHT, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_NINE, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_TEN, Ind_MonitorDashboardLevel);
  cliFSM.defineTransition(CL_MON_DASHBOARD, CL_MON_DASHBOARD, CL_EV_ELEVEN, Ind_MonitorDashboardLevel);

  // Monitor->Node Level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_MON_NODE, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_ERROR, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_HELP, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_EXIT, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MONITOR, CL_EV_PREV, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_GET, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_SET, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_DISPLAY, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_CHANGE, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_ADD_ALARM, Ind_MonitorNodeLevel);     // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_ONE, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_TWO, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_THREE, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_FOUR, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_FIVE, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_SIX, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_SEVEN, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_EIGHT, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_NINE, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_TEN, Ind_MonitorNodeLevel);
  cliFSM.defineTransition(CL_MON_NODE, CL_MON_NODE, CL_EV_ELEVEN, Ind_MonitorNodeLevel);

  // Monitor->Protocols Level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_ERROR, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_HELP, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_EXIT, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MONITOR, CL_EV_PREV, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_GET, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_SET, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_DISPLAY, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_CHANGE, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_ADD_ALARM, Ind_MonitorProtocolsLevel);      //OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_ONE, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_TWO, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_THREE, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_FOUR, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_FIVE, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_SIX, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_SEVEN, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_EIGHT, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_NINE, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_TEN, Ind_MonitorProtocolsLevel);
  cliFSM.defineTransition(CL_MON_PROTOCOLS, CL_MON_PROTOCOLS, CL_EV_ELEVEN, Ind_MonitorProtocolsLevel);


  // Monitor->Cache Level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_MON_CACHE, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_ERROR, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_HELP, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_EXIT, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MONITOR, CL_EV_PREV, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_GET, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_SET, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_DISPLAY, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_CHANGE, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_ADD_ALARM, Ind_MonitorCacheLevel);  // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_ONE, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_TWO, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_THREE, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_FOUR, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_FIVE, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_SIX, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_SEVEN, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_EIGHT, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_NINE, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_TEN, Ind_MonitorCacheLevel);
  cliFSM.defineTransition(CL_MON_CACHE, CL_MON_CACHE, CL_EV_ELEVEN, Ind_MonitorCacheLevel);


  // Monitor->Other Level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_MON_OTHER, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_ERROR, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_HELP, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_EXIT, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MONITOR, CL_EV_PREV, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_GET, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_SET, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_DISPLAY, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_CHANGE, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_ADD_ALARM, Ind_MonitorOtherLevel);  // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_ONE, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_TWO, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_THREE, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_FOUR, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_FIVE, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_SIX, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_SEVEN, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_EIGHT, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_NINE, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_TEN, Ind_MonitorOtherLevel);
  cliFSM.defineTransition(CL_MON_OTHER, CL_MON_OTHER, CL_EV_ELEVEN, Ind_MonitorOtherLevel);

  // Configure level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONFIGURE, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_ERROR, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_HELP, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_EXIT, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_BASE, CL_EV_PREV, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_GET, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_SET, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_DISPLAY, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_CHANGE, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_ADD_ALARM, Ind_ConfigureLevel);     // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_SERVER, CL_EV_ONE, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_PROTOCOLS, CL_EV_TWO, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_CACHE, CL_EV_THREE, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_SECURITY, CL_EV_FOUR, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_LOGGING, CL_EV_FIVE, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_ROUTING, CL_EV_SIX, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_HOSTDB, CL_EV_SEVEN, Ind_ConfigureLevel);
  /* cliFSM.defineTransition(CL_CONFIGURE, CL_CONF_SNAPSHOTS, CL_EV_SEVEN, Ind_ConfigureLevel); */
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_EIGHT, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_NINE, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_TEN, Ind_ConfigureLevel);
  cliFSM.defineTransition(CL_CONFIGURE, CL_CONFIGURE, CL_EV_ELEVEN, Ind_ConfigureLevel);

  // Configure->Server level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_SERVER, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_ERROR, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_HELP, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_EXIT, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_GET, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_SET, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_DISPLAY, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_CHANGE, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_ADD_ALARM, Ind_ConfigureServerLevel);   // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_ONE, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_TWO, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_THREE, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_FOUR, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_FIVE, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_SIX, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_SEVEN, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_EIGHT, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_NINE, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_TEN, Ind_ConfigureServerLevel);
  cliFSM.defineTransition(CL_CONF_SERVER, CL_CONF_SERVER, CL_EV_ELEVEN, Ind_ConfigureServerLevel);

  // Configure->Protocols level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_ERROR, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_HELP, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_EXIT, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_GET, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_SET, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_DISPLAY, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_CHANGE, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_ADD_ALARM, Ind_ConfigureProtocolsLevel);  //OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_ONE, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_TWO, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_THREE, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_FOUR, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_FIVE, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_SIX, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_SEVEN, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_EIGHT, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_NINE, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_TEN, Ind_ConfigureProtocolsLevel);
  cliFSM.defineTransition(CL_CONF_PROTOCOLS, CL_CONF_PROTOCOLS, CL_EV_ELEVEN, Ind_ConfigureProtocolsLevel);

  // Configure->Cache level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_CACHE, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_ERROR, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_HELP, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_EXIT, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_GET, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_SET, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_DISPLAY, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_CHANGE, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_ADD_ALARM, Ind_ConfigureCacheLevel);      //OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_ONE, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_TWO, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_THREE, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_FOUR, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_FIVE, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_SIX, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_SEVEN, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_EIGHT, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_NINE, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_TEN, Ind_ConfigureCacheLevel);
  cliFSM.defineTransition(CL_CONF_CACHE, CL_CONF_CACHE, CL_EV_ELEVEN, Ind_ConfigureCacheLevel);

  // Configure->Security level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_ERROR, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_HELP, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_EXIT, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_GET, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_SET, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_DISPLAY, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_CHANGE, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_ADD_ALARM, Ind_ConfigureSecurityLevel);     //OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_ONE, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_TWO, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_THREE, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_FOUR, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_FIVE, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_SIX, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_SEVEN, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_EIGHT, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_NINE, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_TEN, Ind_ConfigureSecurityLevel);
  cliFSM.defineTransition(CL_CONF_SECURITY, CL_CONF_SECURITY, CL_EV_ELEVEN, Ind_ConfigureSecurityLevel);

  // Configure->Routing level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_ERROR, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_HELP, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_EXIT, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_GET, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_SET, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_DISPLAY, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_CHANGE, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_ADD_ALARM, Ind_ConfigureRoutingLevel);        //OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_ONE, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_TWO, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_THREE, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_FOUR, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_FIVE, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_SIX, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_SEVEN, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_EIGHT, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_NINE, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_TEN, Ind_ConfigureRoutingLevel);
  cliFSM.defineTransition(CL_CONF_ROUTING, CL_CONF_ROUTING, CL_EV_ELEVEN, Ind_ConfigureRoutingLevel);

  // Configure->HostDB level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_ERROR, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_HELP, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_EXIT, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_GET, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_SET, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_DISPLAY, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_CHANGE, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_ADD_ALARM, Ind_ConfigureHostDBLevel);   // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_ONE, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_TWO, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_THREE, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_FOUR, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_FIVE, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_SIX, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_SEVEN, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_EIGHT, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_NINE, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_TEN, Ind_ConfigureHostDBLevel);
  cliFSM.defineTransition(CL_CONF_HOSTDB, CL_CONF_HOSTDB, CL_EV_ELEVEN, Ind_ConfigureHostDBLevel);

  // Configure->Logging level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_ERROR, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_HELP, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_EXIT, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_GET, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_SET, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_DISPLAY, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_CHANGE, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_ADD_ALARM, Ind_ConfigureLoggingLevel);        // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_ONE, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_TWO, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_THREE, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_FOUR, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_FIVE, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_SIX, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_SEVEN, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_EIGHT, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_NINE, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_TEN, Ind_ConfigureLoggingLevel);
  cliFSM.defineTransition(CL_CONF_LOGGING, CL_CONF_LOGGING, CL_EV_ELEVEN, Ind_ConfigureLoggingLevel);

  // Configure->Snapshots level
  //                      source,  dest,   event,  index 
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_BASE, INTERNAL_ERROR, Ind_InternalError);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_ERROR, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_HELP, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_EXIT, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONFIGURE, CL_EV_PREV, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_GET, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_SET, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_DISPLAY, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_CHANGE, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_ADD_ALARM, Ind_ConfigureSnapshotsLevel);  // OEM_ALARM
  // events tied to number selection
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_ONE, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_TWO, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_THREE, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_FOUR, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_FIVE, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_SIX, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_SEVEN, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_EIGHT, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_NINE, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_TEN, Ind_ConfigureSnapshotsLevel);
  cliFSM.defineTransition(CL_CONF_SNAPSHOTS, CL_CONF_SNAPSHOTS, CL_EV_ELEVEN, Ind_ConfigureSnapshotsLevel);

  // Get ready to parse input
  inputBuf[1024] = '\0';
  cmdTok.setMaxTokens(3);       //input form -> <batch/interactive> <command> <args> 

  // process command from 'cli'
  // NOTE: will need to change the protocol a little bit
  //       when handling connections from a telnet->port session.
  while (event != CL_EV_EXIT) {
    event = CL_EV_ERROR;

    // reuse input/output buffers
    input.reUse();
    output.reUse();

    // read input from 'command line client'
    do {
      readResult = cli_read(cliFD, inputBuf, 1024);
      if (readResult > 0) {
        input.copyFrom(inputBuf, strlen(inputBuf));
      }
    } while (readResult == 1024);

    if (readResult < 0 || input.spaceUsed() <= 0) {
      ink_close_socket(cliFD);
      return;
    }
    // parse command request from client
    cmdTok.Initialize(input.bufPtr(), COPY_TOKS);

    cli_data.cmdmode = (char *) cmdTok[0];      // (b)atch/(i)nterative
    cli_data.command = (char *) cmdTok[1];      // command
    cli_data.args = (char *) cmdTok[2]; // args to command
    cli_data.output = &output;  // output text buffer
    cli_data.advui = pContext->AdvUIEnabled;
    cli_data.featset = pContext->FeatureSet;

    Debug("cli", "handleCLI: cmdmode=%s, command=%s, args=%s \n",
          cli_data.cmdmode ? cli_data.cmdmode : "NULL",
          cli_data.command ? cli_data.command : "NULL", cli_data.args ? cli_data.args : "NULL");

    // make sure the command mode and command to execute exits
    if (cli_data.cmdmode == NULL || cli_data.command == NULL) {
      close(cliFD);
      return;
    }
    // make sure command mode is valid
    if (strcasecmp(cli_data.cmdmode, "i") != 0 && strcasecmp(cli_data.cmdmode, "b") != 0) {
      close(cliFD);
      return;
    }
    // find event to pass to FSM i.e. command -> event
    // for now do simple string compares for every possible command.
    // May need to change to a faster/robust mechanism if needed.

    // all levels, include ascii version
    if (strcasecmp(cli_data.command, "help") == 0 || strcasecmp(cli_data.command, "?") == 0) {
      Debug("cli", "event HELP \n");
      event = CL_EV_HELP;
    } else if (strcasecmp(cli_data.command, "exit") == 0 || strcasecmp(cli_data.command, "quit") == 0) {
      Debug("cli", "event EXIT \n");
      event = CL_EV_EXIT;
    } else if (strcasecmp(cli_data.command, ".") == 0) {
      Debug("cli", "event PREV \n");
      event = CL_EV_PREV;
    } else if (strcasecmp(cli_data.command, "get") == 0) {
      Debug("cli", "event GET \n");
      event = CL_EV_GET;
    } else if (strcasecmp(cli_data.command, "set") == 0) {
      Debug("cli", "event SET \n");
      event = CL_EV_SET;
    } else if (strcasecmp(cli_data.command, "display") == 0 || strcasecmp(cli_data.command, "alarms") == 0) {
      // in the dashboard handler       
      Debug("cli", "event DISPLAY \n");
      event = CL_EV_DISPLAY;
    } else if (strcasecmp(cli_data.command, "add_alarm") == 0) {
      // OEM_ALARM
      Debug("cli", "customized ALARM added \n");
      // created a new event = CL_EV_ADD_ALARM which 
      // calls AddAlarm function; add this transition to the FSM
      event = CL_EV_ADD_ALARM;
    } else if (strcasecmp(cli_data.command, "change") == 0 || strcasecmp(cli_data.command, "resolve") == 0) {
      // overload 'change' to resolve alarms
      // in the dashboard handler
      Debug("cli", "event CHANGE \n");
      event = CL_EV_CHANGE;
    } else if (strcasecmp(cli_data.command, "query_deadhosts") == 0) {
      Debug("cli", "event QUERY_DEADHOSTS");
      event = CL_EV_QUERY_DEADHOSTS;
    } else if (strcasecmp(cli_data.command, "1") == 0) {
      Debug("cli", "event ONE \n");
      event = CL_EV_ONE;
    } else if (strcasecmp(cli_data.command, "2") == 0) {
      Debug("cli", "event TWO \n");
      event = CL_EV_TWO;
    } else if (strcasecmp(cli_data.command, "3") == 0) {
      Debug("cli", "event THREE \n");
      event = CL_EV_THREE;
    } else if (strcasecmp(cli_data.command, "4") == 0) {
      Debug("cli", "event FOUR \n");
      event = CL_EV_FOUR;
    } else if (strcasecmp(cli_data.command, "5") == 0) {
      Debug("cli", "event FIVE \n");
      event = CL_EV_FIVE;
    } else if (strcasecmp(cli_data.command, "6") == 0) {
      Debug("cli", "event SIX \n");
      event = CL_EV_SIX;
    } else if (strcasecmp(cli_data.command, "7") == 0) {
      Debug("cli", "event SEVEN \n");
      event = CL_EV_SEVEN;
    } else if (strcasecmp(cli_data.command, "8") == 0) {
      Debug("cli", "event EIGHT \n");
      event = CL_EV_EIGHT;
    } else if (strcasecmp(cli_data.command, "9") == 0) {
      Debug("cli", "event NINE \n");
      event = CL_EV_NINE;
    } else if (strcasecmp(cli_data.command, "10") == 0) {
      Debug("cli", "event TEN \n");
      event = CL_EV_TEN;
    } else if (strcasecmp(cli_data.command, "11") == 0) {
      Debug("cli", "event ELEVEN \n");
      event = CL_EV_ELEVEN;
    } else {
      // unknown command mode
      Debug("cli", "event ERROR \n");
      event = CL_EV_ERROR;
    }

    cli_data.cevent = event;    // command -> event

    // execute transition and associated actions
    if (cliFSM.control(event, (void *) &cli_data) == FALSE) {
      ink_close_socket(cliFD);
      return;
    }
    // send response back to client
    if (event != CL_EV_EXIT && (cli_write(cliFD, output.bufPtr(), output.spaceUsed()) < 0)) {
      ink_close_socket(cliFD);
      return;
    } else if (CL_EV_EXIT == event) {
      ink_close_socket(cliFD);
    }
  }                             // end while

  return;
}                               // end handleCLI()

void
handleOverseer(int fd, int mode)
{
  char *ok = "Ok", buf[8192], reply[2048];
  RecDataT mtype = RECD_NULL;

  bool command_allowed;

  static char *help_lines[] = {
    "",
    "  Traffic Server Overseer Port",
    "",
    "  commands:",
    "    get <variable-list>",
    "    set <variable-name> = \"<value>\"",
    "    help",
    "    exit",
    "",
    "  example:",
    "",
    "    Ok",
    "    get proxy.node.cache.contents.bytes_free",
    "    proxy.node.cache.contents.bytes_free = \"56616048\"",
    "    Ok",
    "",
    "  Variable lists are etc/trafficserver/stats records, separated by commas",
    "",
    NULL
  };

  // check the mode
  ink_debug_assert((mode == 1) || (mode == 2));

  memset(buf, 0, 8192);
  mgmt_writeline(fd, ok, strlen(ok));
  while (mgmt_readline(fd, buf, 8192) > 0) {

    command_allowed = true;

    char *tmp = buf + strlen((char *) buf) - 1;
    while (isascii(*tmp) && isspace(*tmp)) {
      *tmp = '\0';
      tmp--;
    }

    if (strncasecmp(buf, "get ", 4) == 0) {
      char *vars = buf + 4;

      if (vars) {
        char *cur, *lasts;

        cur = ink_strtok_r(vars, ",", &lasts);
        while (cur) {

          while (isspace(*cur))
            ++cur;              // strip whitespace
          // BUG 53327
          RecAccessT access = RECA_NO_ACCESS;
          RecGetRecordAccessType(cur, &access);
          if (access == RECA_NO_ACCESS) {
            ink_snprintf(reply, sizeof(reply), "%s is unavailable", cur);
            mgmt_writeline(fd, reply, strlen(reply));
            cur = ink_strtok_r(NULL, ",", &lasts);
            continue;
          }

          RecDataT mtype = RECD_NULL;
          if (RecGetRecordDataType(cur, &mtype) == REC_ERR_OKAY) {
            switch (mtype) {
            case RECD_COUNTER:{
                RecCounter val;
                RecGetRecordCounter(cur, &val);
                ink_snprintf(reply, sizeof(reply), "%s = \"%lld\"", cur, val);
                break;
              }
            case RECD_INT:{
                RecInt val;
                RecGetRecordInt(cur, &val);
                ink_snprintf(reply, sizeof(reply), "%s = \"%lld\"", cur, val);
                break;
              }
            case RECD_LLONG:{
                RecLLong val;
                RecGetRecordLLong(cur, &val);
                ink_snprintf(reply, sizeof(reply), "%s = \"%lld\"", cur, val);
                break;
              }
            case RECD_FLOAT:{
                RecFloat val;
                RecGetRecordFloat(cur, &val);
                ink_snprintf(reply, sizeof(reply), "%s = \"%f\"", cur, val);
                break;
              }
            case RECD_STRING:{
                RecString val;
                RecGetRecordString_Xmalloc(cur, &val);
                if (val) {
                  ink_snprintf(reply, sizeof(reply), "%s = \"%s\"", cur, val);
                  xfree(val);
                } else {
                  ink_snprintf(reply, sizeof(reply), "%s = \"\"", cur);
                }
                break;
              }
            default:
              ink_snprintf(reply, sizeof(reply), "%s = UNDEFINED", cur);
              break;
            }
          } else {
            ink_snprintf(reply, sizeof(reply), "%s = UNDEFINED", cur);
          }
          mgmt_writeline(fd, reply, strlen(reply));
          cur = ink_strtok_r(NULL, ",", &lasts);
        }
      } else {
        mgmt_writeline(fd, ok, strlen(ok));
        continue;
      }
    } else if ((strncasecmp(buf, "set ", 4) == 0) && (command_allowed = (mode == 2))) {
      const char adminpasswdVar[] = "proxy.config.admin.admin_password";
      const char guestpasswdVar[] = "proxy.config.admin.guest_password";
      char *config_value = NULL;
      char newpass1_md5_str[33] = "";   // used later for computing password md5
      int i;
      bool err = false, collapse_quotes = false;
      char *var, *value, tmp_value[1024];

      var = buf + 4;
      for (; *var && isspace(*var); ++var);
      for (i = 0; var[i] && !isspace(var[i]) && var[i] != '='; i++);
      var[i++] = '\0';

      while (var[i]) {
        if (isspace(var[i]) || var[i] == '=' || var[i] == '"') {
          i++;
        } else {
          break;
        }
      }
      value = &var[i];

      for (i = 0; value[i] && value[i] != '"'; i++) {
        if (value[i] == '\\' && value[i + 1] && value[i + 1] == '"') {
          collapse_quotes = true;
          i++;
          continue;
        }
      }
      value[i] = '\0';

      if (collapse_quotes) {
        int j = 0;
        for (i = 0; value[i]; i++) {
          if (value[i] == '\\' && value[i + 1] && value[i + 1] == '"') {
            i++;
          }
          tmp_value[j++] = value[i];
        }
        value = &tmp_value[0];
      }

      config_value = value;     // default

      int rec_err = RecGetRecordDataType(var, &mtype);
      err = (rec_err != REC_ERR_OKAY);

      if (err) {
        ink_snprintf(reply, sizeof(reply), "%s = UNDEFINED", var);
        mgmt_writeline(fd, reply, strlen(reply));
        mgmt_writeline(fd, ok, strlen(ok));
        continue;
      }


      if (strncmp(adminpasswdVar, var, strlen(adminpasswdVar)) == 0
          || strncmp(guestpasswdVar, var, strlen(guestpasswdVar)) == 0) {
        // changing passwords
        char *newpass1 = value;
        INK_DIGEST_CTX context;
        char newpass1_md5[16];

        ink_code_incr_md5_init(&context);
        ink_code_incr_md5_update(&context, newpass1, strlen(newpass1));
        ink_code_incr_md5_final(newpass1_md5, &context);
        ink_code_md5_stringify(newpass1_md5_str, sizeof(newpass1_md5_str), newpass1_md5);
        newpass1_md5_str[23] = '\0';    /* only use 23 characters */
        config_value = newpass1_md5_str;        // encrypted passord
      }

      Debug("cli", "handleOverSeer: set config=%s, value=%s \n", var, config_value);

      RecDataT mtype = RECD_NULL;
      RecGetRecordDataType(var, &mtype);
      switch (mtype) {
      case RECD_COUNTER:{
          RecCounter val = (RecCounter) ink_atoll(config_value);
          RecSetRecordCounter(var, val);
          break;
        }
      case RECD_INT:{
          RecInt val = (RecInt) ink_atoll(config_value);
          RecSetRecordInt(var, val);
          break;
        }
      case RECD_LLONG:{
          RecLLong val = (RecLLong) ink_atoll(config_value);
          RecSetRecordLLong(var, val);
          break;
        }
      case RECD_FLOAT:{
          RecFloat val = (RecFloat) atof(config_value);
          RecSetRecordFloat(var, val);
          break;
        }
      case RECD_STRING:{
          if ((strcmp(config_value, "NULL") == 0) || (strcmp(config_value, "") == 0)) {
            RecSetRecordString(var, NULL);
          } else {
            RecSetRecordString(var, config_value);
          }
          break;
        }
      default:
        // This handles:
        // RECD_NULL, RECD_STAT_CONST, RECD_STAT_FX, RECD_MAX
        break;
      }

    } else if ((strncasecmp(buf, "exit", 4) == 0) || (strncasecmp(buf, "quit", 4) == 0)) {
      break;
    } else if (strncasecmp(buf, "rec", 3) == 0) {
      mgmt_writeline(fd, "librecords", 10);
    } else if (strncasecmp(buf, "help", 4) == 0) {
      int i;
      for (i = 0; help_lines[i] != NULL; i++) {
        mgmt_writeline(fd, help_lines[i], strlen(help_lines[i]));
      }
    } else if ((strncasecmp(buf, "reread config files", 19) == 0) && (command_allowed = (mode == 2))) {
      configFiles->rereadConfig();
      lmgmt->signalEvent(MGMT_EVENT_PLUGIN_CONFIG_UPDATE, "*");
    } else if ((strncasecmp(buf, "roll log files", 14) == 0) && (command_allowed = (mode == 2))) {
      lmgmt->rollLogFiles();
    } else if ((strncasecmp(buf, "bounce local process", 20) == 0) && (command_allowed = (mode == 2))) {
      lmgmt->processBounce();
    }
    else if ((strncasecmp(buf, "restart local process", 21) == 0) && (command_allowed = (mode == 2))) {
      lmgmt->processRestart();
    }
    else {
      if (command_allowed)
        mgmt_writeline(fd, "Unknown Command", 15);
      else
        mgmt_writeline(fd, "Command Disabled", 16);
    }
    mgmt_writeline(fd, ok, strlen(ok));
    memset(buf, 0, 8192);
  }
  ink_close_socket(fd);
  return;
}
