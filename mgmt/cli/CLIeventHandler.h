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

#ifndef _CMDLINE_EVENTHANDLER_H
#define _CMDLINE_EVENTHANDLER_H

#include <stdio.h>
#include "AbsEventHandler.h"    /* Abstract base class for event handler */
#include "TextBuffer.h"

/* Define list of possible command line states */
enum cmdline_states
{
  CL_BASE = 0,                  /* 1. Base command line */
  CL_MONITOR,                   /* 2. Monitor mode */
  CL_CONFIGURE,                 /* 3. Configuration mode */
  CL_MON_DASHBOARD,             /* 4. Monitor->Dashboard */
  CL_MON_NODE,                  /* 5. Monitor->Node */
  CL_MON_PROTOCOLS,             /* 6. Monitor->Protocols */
  CL_MON_CACHE,                 /* 7. Monitor->Cache */
  CL_MON_OTHER,                 /* 8. Monitor->Other */
  CL_CONF_SERVER,               /* 9. Configure->Server */
  CL_CONF_PROTOCOLS,            /* 10. Configure->Protocols */
  CL_CONF_CACHE,                /* 11. Configure->Cache */
  CL_CONF_SECURITY,             /* 12. Configure->Security */
  CL_CONF_HOSTDB,               /* 13. Configure->Hostdb */
  CL_CONF_LOGGING,              /* 14. Configure->logging */
  CL_CONF_SNAPSHOTS,            /* 15. Configure->snapshots */
  CL_CONF_ROUTING               /* 16. Configure->routing */
};


/* Define list of possible events to command line */
enum cmdline_events             /* 20 events */
{
  /* INTERNAL_ERROR = 0, for unhandled events */
  /* all levels  - 8 */
  CL_EV_ERROR = 1,
  CL_EV_HELP,                   /* all levels */
  CL_EV_EXIT,                   /* all levels */
  CL_EV_PREV,                   /* all levels, BaseLevel should ignore */
  CL_EV_GET,                    /* ?all levels? , only really used in batch mode */
  CL_EV_SET,                    /* ?all levels? */
  CL_EV_DISPLAY,                /* ?all levels? */
  CL_EV_CHANGE,
  CL_EV_QUERY_DEADHOSTS,        /* only base level */
  /* generic events based on option number - 11 */
  CL_EV_ADD_ALARM,              /* OEM_SUN custom alarm feature */
  CL_EV_ONE,
  CL_EV_TWO,
  CL_EV_THREE,
  CL_EV_FOUR,
  CL_EV_FIVE,
  CL_EV_SIX,
  CL_EV_SEVEN,
  CL_EV_EIGHT,
  CL_EV_NINE,
  CL_EV_TEN,
  CL_EV_ELEVEN
};

/* Event handlers indexes i.e. each state handles events  */
enum Handler_indexes
{
  Ind_InternalError,            /* handle Internal Errors e.g. unhandled events */
  Ind_BaseLevel,
  Ind_MonitorLevel,
  Ind_MonitorDashboardLevel,
  Ind_MonitorNodeLevel,
  Ind_MonitorProtocolsLevel,
  Ind_MonitorCacheLevel,
  Ind_MonitorOtherLevel,
  Ind_ConfigureLevel,
  Ind_ConfigureServerLevel,
  Ind_ConfigureProtocolsLevel,
  Ind_ConfigureCacheLevel,
  Ind_ConfigureSecurityLevel,
  Ind_ConfigureHostDBLevel,
  Ind_ConfigureLoggingLevel,
  Ind_ConfigureSnapshotsLevel,
  Ind_ConfigureRoutingLevel
};


/* data structure used to pass data to event handlers */
typedef struct CLIdata
{
  char *cmdmode;                /* command mode i.e. batch(b) or interactive(i) */
  char *cmdprompt;              /* what prompt should look like e.g '->' */
  char *command;                /* command string itself */
  char *args;                   /* arugments passed to 'command' */
  textBuffer *output;           /* output buffer */
  cmdline_events cevent;        /* event */
  int advui;                    /* which UI */
  int featset;                  /* feature set */
} CLI_DATA;

/* Command Line event handling  class derived from an abstract event handler class */
class CmdLine_EventHandler:public AbsEventHandler
{
private:
  /* variables */
  char *cmdmode;                /* command mode i.e. batch(b) or interactive(i) */
  const char *cmdprompt;              /* what prompt should look like e.g '->' */
  const char *dcmdprompt;             /* default prompt */
  char *args;                   /* arugments passed to this level */
  cmdline_states curr_state;    /* current command line state */

  /* Redefinition of the abstract base class's pure virtual function
   * ref: AbsEvenhandler.h   */
  void FillHandlersArray(void);

  /*  Event handlers - make some actions associated with a transition */
  bool handleInternalError(void *cdata);
  bool BaseLevel(void *cdata);
  bool MonitorLevel(void *cdata);
  bool MonitorDashboardLevel(void *cdata);
  bool MonitorProtocolsLevel(void *cdata);
  bool MonitorNodeLevel(void *cdata);
  bool MonitorCacheLevel(void *cdata);
  bool MonitorOtherLevel(void *cdata);
  bool ConfigureLevel(void *cdata);
  bool ConfigureServerLevel(void *cdata);
  bool ConfigureProtocolsLevel(void *cdata);
  bool ConfigureCacheLevel(void *cdata);
  bool ConfigureSecurityLevel(void *cdata);
  bool ConfigureRoutingLevel(void *cdata);
  bool ConfigureHostDBLevel(void *cdata);
  bool ConfigureLoggingLevel(void *cdata);
  bool ConfigureSnapshotsLevel(void *cdata);

  /* copy constructor and assignment operator are private
   *  to prevent their use */
    CmdLine_EventHandler(const CmdLine_EventHandler & rhs);
    CmdLine_EventHandler & operator=(const CmdLine_EventHandler & rhs);

  /* no default constructor */
    CmdLine_EventHandler();

public:
  /* Constructor */
    CmdLine_EventHandler(int inNumberTransitions, char *cmdm = NULL, char *cmdp = NULL, char *largs = NULL);

  /* Destructor */
   ~CmdLine_EventHandler(void);

  /* Member fcns */
  const char *command_prompt();       /* get prompt */
  void command_prompt(const char *new_prompt);        /* set prompt */
  char *arguments();            /* get args */
  void arguments(char *new_args);       /* set args */
  char *command_mode();         /* get command mode */
  void command_mode(char *new_cmode);   /* set command mode */
  cmdline_states current_state();       /* get current state */
  void current_state(cmdline_states new_state); /* set current state */
};

#endif /* _CMDLINE_EVENTHANDLER_H */
