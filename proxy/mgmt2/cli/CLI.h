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
 *  CLI.h - interface to handle server side command line interface
 *  
 * 
 ****************************************************************************/

#ifndef _CLI_H_
#define _CLI_H_

void handleOverseer(int cliFD, int mode);
#include "inktomi++.h"
#include "TextBuffer.h"
#include "CLIeventHandler.h"

#include "P_RecCore.h"

/* Keep globals variables/fcns in their own name space */
struct CLI_globals
{
  /* Level Description */
  typedef struct CLI_LevelDesc
  {
    cmdline_states cmdlevel;    /* command line level */
    const char *cmdprompt;            /* command line level prompt */
  } CLI_LevelDesc;

  /* Variable / Description pairs */
  typedef struct VarNameDesc
  {
    const char *name;                 /* node variable name */
    const char *cname;                /* cluster variable name (if one exits) */
    const char *desc;                 /* variable description */
    const char *format;               /* format string */
    int name_value_width;       /* field width for name value */
    int cname_value_width;      /* field width for cname value */
    int desc_width;             /* width of description field */
    int no_width;               /* width of the number field */
  } VarNameDesc;

  /* some constants */
  enum CLI_CONSTANTS
  {
    CMD_CONST_NUM_LEVELS = 16   /* Current number of command line levels */
  };

  /* Global variables */
  static const CLI_LevelDesc cmdLD[CMD_CONST_NUM_LEVELS];
  static const char *successStr;
  static const char *failStr;
  static const char *unknownCmd;
  static const char *argNum;
  static const char *varNotFound;
  static const char *sep1;
  static const char *sep2;

  /* CLIhelp.cc */
  static void Help(textBuffer * output, /* IN/OUT: output buffer */
                   cmdline_states hlevel,       /*     IN: command level */
                   int advui,   /*     IN: */
                   int featset /*     IN: */ );

  /* CLI.cc */
  static void set_response(textBuffer * output, /* IN/OUT: output buffer */
                           const char *header,  /*     IN: header */
                           const char *trailer, /*     IN: trailer */
                           cmdline_states plevel /*     IN: command level */ );

  static void set_prompt(textBuffer * output,   /* IN/OUT: output buffer */
                         cmdline_states plevel /*     IN: command level */ );

  static void Get(char *largs,  /*     IN: arguments */
                  textBuffer * output,  /* IN/OUT: output buffer */
                  cmdline_states plevel /*     IN: command level */ );

  static void Set(char *largs,  /*     IN: arguments */
                  textBuffer * output,  /* IN/OUT: output buffer */
                  cmdline_states plevel /*     IN: command level */ );

  static void Change(char *largs,       /*     IN: arguments */
                     const VarNameDesc * desctable,     /*     IN: description table */
                     int ndesctable,    /*     IN: number of descs */
                     textBuffer * output,       /* IN/OUT: output buffer */
                     cmdline_states plevel /*     IN: command level */ );

  static void ReRead(char *largs,       /*     IN: arguments */
                     textBuffer * output,       /* IN/OUT: output buffer */
                     cmdline_states plevel /*     IN: command level */ );

  static void Shutdown(char *largs,     /*     IN: arguments */
                       textBuffer * output,     /* IN/OUT: output buffer */
                       cmdline_states plevel /*     IN: command level */ );

  static void BounceProxies(char *largs,        /*     IN: arguments */
                            textBuffer * output,        /* IN/OUT: output buffer */
                            cmdline_states plevel /*     IN: command level */ );

  static void BounceLocal(char *largs,  /*     IN: arguments */
                          textBuffer * output,  /* IN/OUT: output buffer */
                          cmdline_states plevel /*     IN: command level */ );

  static void ClearStats(char *largs,   /*     IN: arguments */
                         textBuffer * output,   /* IN/OUT: output buffer */
                         bool cluster,  /*     IN: clustering? */
                         cmdline_states plevel /*     IN: command level */ );

  static void TestAlarm(textBuffer * output,    /* IN/OUT: output buffer */
                        cmdline_states plevel /*     IN: command level */ );

  static void ShutdownMgmtL(char *largs,        /*     IN: arguments */
                            textBuffer * output,        /* IN/OUT: output buffer */
                            cmdline_states plevel /*     IN: command level */ );

  static void ShutdownMgmtC(char *largs,        /*     IN: arguments */
                            textBuffer * output,        /* IN/OUT: output buffer */
                            cmdline_states plevel /*     IN: command level */ );

  static void Startup(char *largs,      /*     IN: arguments */
                      textBuffer * output,      /* IN/OUT: output buffer */
                      cmdline_states plevel /*     IN: command level */ );

  // OEM_ALARM
  static void AddAlarm(char *largs,     /* IN; arguments */
                       textBuffer * output,     /* IN/OUT: output buffer */
                       cmdline_states plevel /*   IN: command level */ );

  static void QueryDeadhosts(char *largs,       /* IN; arguments */
                             textBuffer * output,       /* IN/OUT: output buffer */
                             cmdline_states plevel /*   IN: command level */ );
};

/* CLI.cc */
void handleCLI(int cliFD,       /* IN: UNIX domain socket descriptor */
               WebContext * pContext /* IN: */ );


#endif /* _CLI_H_ */
