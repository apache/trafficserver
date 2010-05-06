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
 *  Module: concrete class for comand line interface event handling
 *  
 * 
 ****************************************************************************/

#include "inktomi++.h"
#include "ink_unused.h"    /* MAGIC_EDITING_TAG */
/* system includes */
#include <stdio.h>
#include <stdlib.h>

/* local includes */
#include "CLIeventHandler.h"
#include "Tokenizer.h"
#include "WebMgmtUtils.h"
#include "FileManager.h"
#include "MgmtUtils.h"
#include "LocalManager.h"
#include "CliUtils.h"
#include "CLI.h"

#include "CLImonitor.h"
#include "CLIconfigure.h"
#include "Diags.h"

/* NO default constructor */

/* Constructor */
CmdLine_EventHandler::CmdLine_EventHandler(int inNumberTransitions, char *cmdm, char *cmdp, char *largs)
  :
AbsEventHandler(inNumberTransitions)
{
  cmdmode = cmdm;
  cmdprompt = cmdp;
  dcmdprompt = CLI_globals::cmdLD[CL_BASE].cmdprompt;
  args = largs;
  curr_state = CL_BASE;

  // Call this function in base class from the constructor
  FillHandlersArray();
}

/* Destructor */
CmdLine_EventHandler::~CmdLine_EventHandler(void)
{                               /* We rely on base class to free the function table array */
}

/****************************** Member fcns *******************************/

inline const char *
CmdLine_EventHandler::command_prompt()
{
  return cmdprompt;
}

inline void
CmdLine_EventHandler::command_prompt(const char *new_prompt)
{
  cmdprompt = new_prompt ? new_prompt : NULL;
}

inline char *
CmdLine_EventHandler::arguments()
{
  return args;
}

inline void
CmdLine_EventHandler::arguments(char *new_args)
{
  args = new_args ? new_args : NULL;
}

inline void
CmdLine_EventHandler::command_mode(char *new_cmode)
{
  cmdmode = new_cmode ? new_cmode : NULL;
}

inline cmdline_states
CmdLine_EventHandler::current_state()
{
  return curr_state;
}

inline void
CmdLine_EventHandler::current_state(cmdline_states new_state)
{
  curr_state = new_state;
}

/******************************** Event Handlers *****************************/

//
// Handle unhandled events
//
bool
CmdLine_EventHandler::handleInternalError(void *cdata)
{
  (void) cdata;

  Debug("cli_event", "Handle internal error, possibly not specified transition \n");
  return FALSE;
}                               // end handleInternalError()

//
// Base command Level handling
//
bool
CmdLine_EventHandler::BaseLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter BaseLevel: cli_data->cevent=%d \n", cli_data->cevent);

  // satisfy request if valid
  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_BASE, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_BASE);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Base level
      command_prompt(CLI_globals::cmdLD[CL_BASE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_BASE);
      CLI_globals::Help(cli_data->output, CL_BASE, cli_data->advui, cli_data->featset);
    } else {
      CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_BASE);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ADD_ALARM:        // OEM_ALARM
    CLI_globals::AddAlarm(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_QUERY_DEADHOSTS:
    CLI_globals::QueryDeadhosts(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // Monitor level
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {
      command_prompt(CLI_globals::cmdLD[CL_MONITOR].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MONITOR);
      CLI_globals::Help(cli_data->output, CL_MONITOR, cli_data->advui, cli_data->featset);
    } else {
      CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_BASE);
    }
    break;
  case CL_EV_TWO:              // Configure level
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    } else {
      CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_BASE);
    }
    break;
  case CL_EV_THREE:            // REREAD
    CLI_globals::ReRead(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_FOUR:             // SHUTDOWN
    CLI_globals::Shutdown(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_FIVE:             // STARTUP
    CLI_globals::Startup(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SIX:              // BOUNCE_LOCAL
    CLI_globals::BounceLocal(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SEVEN:            // BOUNCE_CLUSTER
    CLI_globals::BounceProxies(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_EIGHT:            // RESTART_LOCAL
    CLI_globals::ShutdownMgmtL(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_NINE:             // RESTART_CLUSTER
    CLI_globals::ShutdownMgmtC(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:
    // we use this to set an alarm for testing purposes now
    CLI_globals::TestAlarm(cli_data->output, curr_state);
    break;
    // Dont' allow clearing of statistics
  case CL_EV_TEN:              // CLEAR_CLUSTER
    CLI_globals::ClearStats(cli_data->args, cli_data->output, true, curr_state);
    break;
    /* Fall through */
  case CL_EV_ELEVEN:           // CLEAR_NODE
    CLI_globals::ClearStats(cli_data->args, cli_data->output, false, curr_state);
    break;
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_BASE);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting BaseLevel \n");

  return TRUE;
}                               // end BaseLevel()

//
// handle command at monitor level
//
bool
CmdLine_EventHandler::MonitorLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter MonitorLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_MONITOR, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MONITOR);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Base level
      command_prompt(CLI_globals::cmdLD[CL_BASE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_BASE);
      CLI_globals::Help(cli_data->output, CL_BASE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // Dashboard
    Debug("cli_event", "MonitorLevel: entered dashboard case \n");
    command_prompt(CLI_globals::cmdLD[CL_MON_DASHBOARD].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_MON_DASHBOARD);
    CLI_globals::Help(cli_data->output, CL_MON_DASHBOARD, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_TWO:              // Node
    Debug("cli_event", "MonitorLevel: entered node case \n");
    if ((0 == cli_data->advui || 2 == cli_data->advui)) {
      // Not allowed for Simple/RNI UI
      current_state(CL_MONITOR);
      CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MONITOR);
    } else {
      command_prompt(CLI_globals::cmdLD[CL_MON_NODE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MON_NODE);
      CLI_globals::Help(cli_data->output, CL_MON_NODE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_THREE:            // Protocols
    Debug("cli_event", "Monitor: entered protocol case \n");
    command_prompt(CLI_globals::cmdLD[CL_MON_PROTOCOLS].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_MON_PROTOCOLS);
    CLI_globals::Help(cli_data->output, CL_MON_PROTOCOLS, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_FOUR:             // Cache
    Debug("cli_event", "MonitorLevel: entered cache case \n");
    if ((0 == cli_data->advui || 2 == cli_data->advui)) {
      // Not allowed for Simple/RNI UI
      current_state(CL_MONITOR);
      CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MONITOR);
    } else {
      command_prompt(CLI_globals::cmdLD[CL_MON_CACHE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MON_CACHE);
      CLI_globals::Help(cli_data->output, CL_MON_CACHE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_FIVE:             // Other
    Debug("cli_event", "MonitorLevel: entered other case \n");
    command_prompt(CLI_globals::cmdLD[CL_MON_OTHER].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_MON_OTHER);
    CLI_globals::Help(cli_data->output, CL_MON_OTHER, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_DISPLAY:
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MONITOR);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting MonitorLevel \n");

  return TRUE;
}                               // end MonitorLevel()

//
// Handle command at monitor dashboard level
//
bool
CmdLine_EventHandler::MonitorDashboardLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter MonitorDashboardLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_MON_DASHBOARD, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_DASHBOARD);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Monitor level
      command_prompt(CLI_globals::cmdLD[CL_MONITOR].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MONITOR);
      CLI_globals::Help(cli_data->output, CL_MONITOR, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ONE:
    Debug("cli_event", "MonitorDashboardLevel: entered %d case \n", cli_data->cevent);
    CLI_monitor::doMonitorDashboard(cli_data);
    break;
  case CL_EV_TWO:
    /* Fall through */
  case CL_EV_THREE:
    /* Fall through */
  case CL_EV_FOUR:
    /* Fall through */
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_DASHBOARD);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting MonitorDashboardLevel \n");
  return TRUE;
}                               // end MonitorDashboardLevel()


//
// Handle command at monitor Node level
//
bool
CmdLine_EventHandler::MonitorNodeLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter MonitorNodeLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_MON_NODE, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_NODE);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Monitor level
      command_prompt(CLI_globals::cmdLD[CL_MONITOR].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MONITOR);
      CLI_globals::Help(cli_data->output, CL_MONITOR, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // Stats
    /* Fall through */
  case CL_EV_TWO:              // Cache
    /* Fall through */
  case CL_EV_THREE:            // Inprogress
    /* Fall through */
  case CL_EV_FOUR:             // Network
    /* Fall through */
  case CL_EV_FIVE:             // Nameres
    // show node stats
    Debug("cli_event", "MonitorNodeLevel: entered %d case \n", cli_data->cevent);
    CLI_monitor::doMonitorNodeStats(cli_data);
    break;
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_DISPLAY:
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_NODE);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting MonitorNodeLevel \n");

  return TRUE;
}                               // end MonitorNodeLevel()


//
// Handle command at monitor protocol level
//
bool
CmdLine_EventHandler::MonitorProtocolsLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter MonitorProtocolsLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_MON_PROTOCOLS, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_PROTOCOLS);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Monitor level
      command_prompt(CLI_globals::cmdLD[CL_MONITOR].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MONITOR);
      CLI_globals::Help(cli_data->output, CL_MONITOR, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // stats
    /* Fall through */
  case CL_EV_TWO:              // HTTP
    /* Fall through */
  case CL_EV_THREE:            // FTP
    /* Fall through */
  case CL_EV_FOUR:             // ICP
    /* Fall through */
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:              // FIXME: RNI stats section
    Debug("cli_event", "MonitorProtocolsLevel: entered %d case \n", cli_data->cevent);
    if ((0 == cli_data->advui || 2 == cli_data->advui)
        && (CL_EV_ONE != cli_data->cevent && CL_EV_SIX != cli_data->cevent)) {
      // Only allow case CL_EV_ONE or CL_EV_SIX for Simple/RNI UI
      CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_PROTOCOLS);
      break;
    }
    CLI_monitor::doMonitorProtocolStats(cli_data);
    break;
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_DISPLAY:
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_PROTOCOLS);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting MonitorProtocolsLevel \n");

  return TRUE;
}                               // end MonitorProtocolsLevel


//
// Handle command at monitor Cache level
//
bool
CmdLine_EventHandler::MonitorCacheLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter MonitorCacheLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_MON_CACHE, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_CACHE);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Monitor level
      command_prompt(CLI_globals::cmdLD[CL_MONITOR].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MONITOR);
      CLI_globals::Help(cli_data->output, CL_MONITOR, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // Stats
    // show cache stats
    Debug("cli_event", "MonitorCacheLevel: entered %d case \n", cli_data->cevent);
    CLI_monitor::doMonitorCacheStats(cli_data);
    break;
  case CL_EV_TWO:
    /* Fall through */
  case CL_EV_THREE:
    /* Fall through */
  case CL_EV_FOUR:
    /* Fall through */
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_DISPLAY:
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_CACHE);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting MonitorCacheLevel \n");

  return TRUE;
}                               // end MonitorCacheLevel()

//
// Handle command at monitor Other level
//
bool
CmdLine_EventHandler::MonitorOtherLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter MonitorOtherLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_MON_OTHER, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_OTHER);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Monitor level
      command_prompt(CLI_globals::cmdLD[CL_MONITOR].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_MONITOR);
      CLI_globals::Help(cli_data->output, CL_MONITOR, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // stats
    /* Fall through */
  case CL_EV_TWO:              // hostdb
    /* Fall through */
  case CL_EV_THREE:            // dns
    /* Fall through */
  case CL_EV_FOUR:             // cluster
    /* Fall through */
  case CL_EV_FIVE:             // socks
    /* Fall through */
  case CL_EV_SIX:              // logging
    Debug("cli_event", "MonitorOtherLevel: entered %d case \n", cli_data->cevent);
    if ((0 == cli_data->advui || 2 == cli_data->advui)
        && (CL_EV_ONE != cli_data->cevent && CL_EV_TWO != cli_data->cevent
            && CL_EV_THREE != cli_data->cevent && CL_EV_SIX != cli_data->cevent)) {
      // Only allow CL_EV_ONE, CL_EV_TWO, CL_EV_THREE, and CL_EV_SIX for RNI only case
      CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_OTHER);
      break;
    }
    CLI_monitor::doMonitorOtherStats(cli_data);
    break;
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_DISPLAY:
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_MON_OTHER);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting MonitorOtherLevel \n");

  return TRUE;
}                               // end MonitorOtherLevel()


// 
// Handle command at server configuration level
//
bool
CmdLine_EventHandler::ConfigureServerLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureServerLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_SERVER, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SERVER);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SERVER);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_server_desctable, NUM_SERVER_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // Display all server configuration 
    /* Fall through */
  case CL_EV_TWO:              // show server only
    /* Fall through */
  case CL_EV_THREE:            // show web management
    /* Fall through */
  case CL_EV_FOUR:             // show VIP
    /* Fall through */
  case CL_EV_FIVE:             // show auto-configuration
    /* Fall through */
  case CL_EV_SIX:              // show throttling of connections
    /* Fall through */
  case CL_EV_SEVEN:            // show SNMP configuration
    /* Fall through */
  case CL_EV_EIGHT:            // Show Customizable Pages
    Debug("cli_event", "ConfigureServerrLevel: entered %d case \n", cli_data->cevent);
    CLI_configure::doConfigureServer(cli_data);
    break;
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SERVER);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureServerLevel \n");

  return TRUE;
}                               // end ConfigureServerLevel()


// 
// Handle command at server protocols level
//
bool
CmdLine_EventHandler::ConfigureProtocolsLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureProtocolsLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_PROTOCOLS, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_PROTOCOLS);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_PROTOCOLS);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_protocols_desctable, NUM_CONF_PROTOCOLS_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // show protocols configuration 
    /* Fall through */
  case CL_EV_TWO:              // show HTTP configuration
    /* Fall through */
  case CL_EV_THREE:            // show FTP configuration
    /* Fall through */
  case CL_EV_FOUR:
    /* Fall through */
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_PROTOCOLS);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureProtocolsLevel \n");

  return TRUE;
}                               // end ConfigureProtocolsLevel()


// 
// Handle command at server cache level
//
bool
CmdLine_EventHandler::ConfigureCacheLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureCacheLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_CACHE, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_CACHE);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_CACHE);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_cache_desctable, NUM_CONF_CACHE_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // show cache configuration 
    /* Fall through */
  case CL_EV_TWO:              // show cache storage configuration
    /* Fall through */
  case CL_EV_THREE:            // show cache activation configuration
    /* Fall through */
  case CL_EV_FOUR:             // show cache freshness configuration 
    /* Fall through */
  case CL_EV_FIVE:             // show cache variable content configuration
    Debug("cli_event", "ConfigureCacheLevel: entered %d case \n", cli_data->cevent);
    CLI_configure::doConfigureCache(cli_data);
    break;
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_CACHE);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureCacheLevel \n");

  return TRUE;
}                               // end ConfigureCacheLevel()


// 
// Handle command at security configuration level
//
bool
CmdLine_EventHandler::ConfigureSecurityLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureSecurityLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_SECURITY, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SECURITY);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SECURITY);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_security_desctable, NUM_CONF_SECURITY_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // show security configuration 
    /* Fall through */
  case CL_EV_TWO:              // show access configuration 
    /* Fall through */
  case CL_EV_THREE:            // show firewall configuration 
    Debug("cli_event", "ConfigureSecurityLevel: entered %d case \n", cli_data->cevent);
    CLI_configure::doConfigureSecurity(cli_data);
    break;
  case CL_EV_FOUR:
    /* Fall through */
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SECURITY);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureSecurityLevel \n");

  return TRUE;
}                               // end ConfigureSecurityLevel()

// 
// Handle command at routing configuration level
//
bool
CmdLine_EventHandler::ConfigureRoutingLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureRoutingLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_ROUTING, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_ROUTING);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_ROUTING);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_rout_desctable, NUM_CONF_ROUT_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // show routing configuration
    /* Fall through */
  case CL_EV_TWO:              // show parent proxy configuration
    /* Fall through */
  case CL_EV_THREE:            // show ICP configuration
    /* Fall through */
  case CL_EV_FOUR:             // show reverse proxy configuration
    Debug("cli_event", "ConfigureRoutingLevel: entered %d case \n", cli_data->cevent);
    CLI_configure::doConfigureRouting(cli_data);
    break;
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_ROUTING);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureRoutingLevel \n");

  return TRUE;
}                               // end ConfigureRoutingLevel()


// 
// Handle command at hostDB configuration level
//
bool
CmdLine_EventHandler::ConfigureHostDBLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureHostDBLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_HOSTDB, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_HOSTDB);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_HOSTDB);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_hostdb_desctable, NUM_CONF_HOSTDB_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // show HostDB configuration 
    /* Fall through */
  case CL_EV_TWO:              // show host database configuration
    /* Fall through */
  case CL_EV_THREE:            // show DNS configuration
    Debug("cli_event", "ConfigureHostDBLevel: entered %d case \n", cli_data->cevent);
    CLI_configure::doConfigureHostDB(cli_data);
    break;
  case CL_EV_FOUR:
    /* Fall through */
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_HOSTDB);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureHostDBLevel \n");

  return TRUE;
}                               // end ConfigureHostDBLevel()


// 
// Handle command at logging configuration level
//
bool
CmdLine_EventHandler::ConfigureLoggingLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureLoggingLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_LOGGING, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_LOGGING);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_LOGGING);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_logging_desctable, NUM_CONF_LOGGING_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // show Logging configuration 
    /* Fall through */
  case CL_EV_TWO:              // show event logging configuration
    /* Fall through */
  case CL_EV_THREE:            // show log management configuration
    /* Fall through */
  case CL_EV_FOUR:             // show log collation configuration
    /* Fall through */
  case CL_EV_FIVE:             // show Squid format configuration
    /* Fall through */
  case CL_EV_SIX:              // show Netscape common configuration
    /* Fall through */
  case CL_EV_SEVEN:            // show Netscape extended configuration
    /* Fall through */
  case CL_EV_EIGHT:            // show Netscape extended2 configuration
    /* Fall through */
  case CL_EV_NINE:             // show log rolling
    Debug("cli_event", "ConfigureLoggingLevel: entered %d case \n", cli_data->cevent);
    CLI_configure::doConfigureLogging(cli_data);
    break;
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_LOGGING);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureLoggingLevel \n");

  return TRUE;
}                               // end ConfigureLoggingLevel()


// 
// Handle command at snapshots configuration level
// 
// NOTE: not handled currently
//
bool
CmdLine_EventHandler::ConfigureSnapshotsLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureSnapshotsLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONF_SNAPSHOTS, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SNAPSHOTS);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Configure level
      command_prompt(CLI_globals::cmdLD[CL_CONFIGURE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_CONFIGURE);
      //CLI_globals::set_response(cli_data->output, CLI_globals::successStr, " ", CL_CONFIGURE);
      CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_DISPLAY:          // unhandled for now
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_LOGGING);
    break;
  case CL_EV_CHANGE:
    CLI_globals::Change(cli_data->args, CLI_configure::conf_snapshots_desctable, NUM_CONF_SNAPSHOTS_DESCS,
                        cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // Display
    // show Snapshots configuration 
    Debug("cli_event", "ConfigureSnapshotsLevel: entered %d case \n", cli_data->cevent);
    CLI_configure::doConfigureSnapshots(cli_data);
    break;
  case CL_EV_TWO:
    /* Fall through */
  case CL_EV_THREE:
    /* Fall through */
  case CL_EV_FOUR:
    /* Fall through */
  case CL_EV_FIVE:
    /* Fall through */
  case CL_EV_SIX:
    /* Fall through */
  case CL_EV_SEVEN:
    /* Fall through */
  case CL_EV_EIGHT:
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONF_SNAPSHOTS);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureSnapshotsLevel \n");

  return TRUE;
}                               // end ConfigureSnapshotsLevel()


// 
// Handle command at base configuration level
//
bool
CmdLine_EventHandler::ConfigureLevel(void *cdata)
{
  CLI_DATA *cli_data = (CLI_DATA *) cdata;

  Debug("cli_event", "Enter ConfigureLevel: cli_data->cevent=%d \n", cli_data->cevent);

  switch (cli_data->cevent) {
  case CL_EV_HELP:
    CLI_globals::Help(cli_data->output, CL_CONFIGURE, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EXIT:
    // we don't handle exit command on server side
    // since it is handled on client side in 'traffic_cli'
    // will need to handle in case of direct telnet to 
    // say a command line port
    // Ingore for now.
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONFIGURE);
    break;
  case CL_EV_PREV:
    if (strcasecmp(cli_data->cmdmode, "i") == 0) {      // Base level
      command_prompt(CLI_globals::cmdLD[CL_BASE].cmdprompt);
      command_mode(cli_data->cmdmode);
      arguments(cli_data->args);
      current_state(CL_BASE);
      CLI_globals::Help(cli_data->output, CL_BASE, cli_data->advui, cli_data->featset);
    }
    break;
  case CL_EV_GET:
    CLI_globals::Get(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_SET:
    CLI_globals::Set(cli_data->args, cli_data->output, curr_state);
    break;
  case CL_EV_ONE:              // Server
    Debug("cli_event", "ConfigureLevel: entered server case \n");
    command_prompt(CLI_globals::cmdLD[CL_CONF_SERVER].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_CONF_SERVER);
    CLI_globals::Help(cli_data->output, CL_CONF_SERVER, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_TWO:              // Protocols
    Debug("cli_event", "ConfigureLevel: entered protocols case \n");
    command_prompt(CLI_globals::cmdLD[CL_CONF_PROTOCOLS].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_CONF_PROTOCOLS);
    CLI_globals::Help(cli_data->output, CL_CONF_PROTOCOLS, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_THREE:            // Cache
    Debug("cli_event", "ConfigureLevel: entered cache case \n");
    command_prompt(CLI_globals::cmdLD[CL_CONF_CACHE].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_CONF_CACHE);
    CLI_globals::Help(cli_data->output, CL_CONF_CACHE, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_FOUR:             // Security
    Debug("cli_event", "ConfigureLevel: entered security case \n");
    command_prompt(CLI_globals::cmdLD[CL_CONF_SECURITY].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_CONF_SECURITY);
    CLI_globals::Help(cli_data->output, CL_CONF_SECURITY, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_FIVE:             // Logging
    Debug("cli_event", "ConfigureLevel: entered logging case \n");
    command_prompt(CLI_globals::cmdLD[CL_CONF_LOGGING].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_CONF_LOGGING);
    CLI_globals::Help(cli_data->output, CL_CONF_LOGGING, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_SIX:              // Routing
    Debug("cli_event", "ConfigureLevel: entered routing case \n");
    command_prompt(CLI_globals::cmdLD[CL_CONF_ROUTING].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_CONF_ROUTING);
    CLI_globals::Help(cli_data->output, CL_CONF_ROUTING, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_SEVEN:            // HostDB
    Debug("cli_event", "ConfigureLevel: entered hostdb case \n");
    command_prompt(CLI_globals::cmdLD[CL_CONF_HOSTDB].cmdprompt);
    command_mode(cli_data->cmdmode);
    arguments(cli_data->args);
    current_state(CL_CONF_HOSTDB);
    CLI_globals::Help(cli_data->output, CL_CONF_HOSTDB, cli_data->advui, cli_data->featset);
    break;
  case CL_EV_EIGHT:            // Snapshots
    /* Fall through */
  case CL_EV_NINE:
    /* Fall through */
  case CL_EV_TEN:
    /* Fall through */
  case CL_EV_ELEVEN:
    /* Fall through */
  case CL_EV_DISPLAY:
    /* Fall through */
  case CL_EV_CHANGE:
    /* Fall through */
  case CL_EV_ERROR:
    /* Fall through */
  case CL_EV_ADD_ALARM:        // OEM_SUN
    /* Fall through */
  default:
    CLI_globals::set_response(cli_data->output, CLI_globals::failStr, CLI_globals::unknownCmd, CL_CONFIGURE);
    break;
  }                             // end switch

  Debug("cli_event", "Exiting ConfigureLevel \n");

  return TRUE;
}                               // end ConfigureLevel()

//
//  private Member fcn 
//
void
CmdLine_EventHandler::FillHandlersArray(void)
{                               // setup array of event handlers
  functions[Ind_InternalError]
    = (FuncAbsTransition) & CmdLine_EventHandler::handleInternalError;
  functions[Ind_BaseLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::BaseLevel;
  functions[Ind_MonitorLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::MonitorLevel;
  functions[Ind_MonitorDashboardLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::MonitorDashboardLevel;
  functions[Ind_MonitorNodeLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::MonitorNodeLevel;
  functions[Ind_MonitorProtocolsLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::MonitorProtocolsLevel;
  functions[Ind_MonitorCacheLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::MonitorCacheLevel;
  functions[Ind_MonitorOtherLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::MonitorOtherLevel;
  functions[Ind_ConfigureLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureLevel;
  functions[Ind_ConfigureServerLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureServerLevel;
  functions[Ind_ConfigureProtocolsLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureProtocolsLevel;
  functions[Ind_ConfigureCacheLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureCacheLevel;
  functions[Ind_ConfigureSecurityLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureSecurityLevel;
  functions[Ind_ConfigureHostDBLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureHostDBLevel;
  functions[Ind_ConfigureLoggingLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureLoggingLevel;
  functions[Ind_ConfigureSnapshotsLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureSnapshotsLevel;
  functions[Ind_ConfigureRoutingLevel]
    = (FuncAbsTransition) & CmdLine_EventHandler::ConfigureRoutingLevel;
}                               // end FillHandlersArray()
