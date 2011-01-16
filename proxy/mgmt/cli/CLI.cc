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

#include "libts.h"
#include "ink_platform.h"
#include "ink_unused.h"  /* MAGIC_EDITING_TAG */

#include "Main.h"
#include "Tokenizer.h"
#include "TextBuffer.h"
#include "WebMgmtUtils.h"
#include "FileManager.h"
#include "MgmtUtils.h"
#include "CliUtils.h"
#include "CLI.h"

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
  const CLI_globals::CLI_LevelDesc
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
  NOWARN_UNUSED(largs);
#ifdef NOT_WORKING  
  int fd = send_cli_congest_request("list");
  if (fd < 0) {                 // error
    CLI_globals::set_response(output, CLI_globals::failStr, "query for congested servers failed", plevel);
  } else {
    output->copyFrom(CLI_globals::successStr, strlen(CLI_globals::successStr));
    set_prompt(output, plevel);

    int nread;
    char response[MAX_BUF_READ_SIZE];
    memset(response, 0, MAX_BUF_READ_SIZE);
    while ((nread = read_socket(fd, response, MAX_BUF_READ_SIZE)) > 0) {
      output->copyFrom(response, nread);
      if (nread < MAX_BUF_READ_SIZE)
        break;
      memset(response, 0, MAX_BUF_READ_SIZE);
    }
    if (send_exit_request(fd) < 0) {    // also closes the fd
      Debug("cli", "[QueryDeadhosts] error closing RAF connection");
    }
  }
#else
  CLI_globals::set_response(output, CLI_globals::failStr, "query for congested servers failed", plevel);
#endif
}

const int MaxNumTransitions = 367;      // Maximum number of transitions in table

void
handleOverseer(int fd, int mode)
{
  const char *ok = "Ok";
  char buf[8192], reply[2048];
  RecDataT mtype;

  bool command_allowed;

  static const char *help_lines[] = {
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
            snprintf(reply, sizeof(reply), "%s is unavailable", cur);
            mgmt_writeline(fd, reply, strlen(reply));
            cur = ink_strtok_r(NULL, ",", &lasts);
            continue;
          }

          mtype = RECD_NULL;
          if (RecGetRecordDataType(cur, &mtype) == REC_ERR_OKAY) {
            switch (mtype) {
            case RECD_COUNTER:{
                RecCounter val;
                RecGetRecordCounter(cur, &val);
                snprintf(reply, sizeof(reply), "%s = \"%" PRId64 "\"", cur, val);
                break;
              }
            case RECD_INT:{
                RecInt val;
                RecGetRecordInt(cur, &val);
                snprintf(reply, sizeof(reply), "%s = \"%" PRId64 "\"", cur, val);
                break;
              }
            case RECD_FLOAT:{
                RecFloat val;
                RecGetRecordFloat(cur, &val);
                snprintf(reply, sizeof(reply), "%s = \"%f\"", cur, val);
                break;
              }
            case RECD_STRING:{
                RecString val;
                RecGetRecordString_Xmalloc(cur, &val);
                if (val) {
                  snprintf(reply, sizeof(reply), "%s = \"%s\"", cur, val);
                  xfree(val);
                } else {
                  snprintf(reply, sizeof(reply), "%s = \"\"", cur);
                }
                break;
              }
            default:
              snprintf(reply, sizeof(reply), "%s = UNDEFINED", cur);
              break;
            }
          } else {
            snprintf(reply, sizeof(reply), "%s = UNDEFINED", cur);
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
        snprintf(reply, sizeof(reply), "%s = UNDEFINED", var);
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

      mtype = RECD_NULL;
      RecGetRecordDataType(var, &mtype);
      switch (mtype) {
      case RECD_COUNTER:{
          RecCounter val = (RecCounter) ink_atoi64(config_value);
          RecSetRecordCounter(var, val);
          break;
        }
      case RECD_INT:{
          RecInt val = (RecInt) ink_atoi64(config_value);
          RecSetRecordInt(var, val);
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
  close_socket(fd);
  return;
}
