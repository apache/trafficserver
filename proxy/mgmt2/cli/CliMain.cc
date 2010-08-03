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
 *  CliMain.cc - A simple client to communicate to local manager
 *
 *
 ****************************************************************************/

#include "inktomi++.h"

#include "ink_args.h"
#include "I_Layout.h"
#include "I_Version.h"
#include "Tokenizer.h"
#include "TextBuffer.h"
#include "CliUtils.h"
#include "clientCLI.h"

#ifdef _WIN32

bool
initWinSock(void)
{
  /* initialize WINSOCK 2.0 */
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  wVersionRequested = MAKEWORD(2, 0);
  err = WSAStartup(wVersionRequested, &wsaData);
  if (err != 0) {
    return false;
  }

  if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 0) {
    WSACleanup();
    return false;
  }

  return true;
}

#endif // _WIN32

static const char *programName;

static int interactiveMode;
static char readVar[255];
static char setVar[255];
static char varValue[255];
static int reRead;
static int Shutdown;
static int BounceCluster;
static int BounceLocal;
static int QueryDeadhosts;
static int Startup;
static int ShutdownMgmtCluster;
static int ShutdownMgmtLocal;
static int ClearCluster;
static int ClearNode;
static int timeout_arg;

static void
handleArgInvocation(clientCLI * cli)
{
  char bufToLm[1024];           // buffer to send request to Local Manager
  textBuffer response(512);     // textBuffer object to hold response
  char *responseStr;            // actual response string
  ink_hrtime timeLeft = -1;
  bool printResponse = false;
  Tokenizer respTok(";");
  const char *status = NULL;
  const char *resp = NULL;
  int bufRemaining = sizeof(bufToLm) - 1;

  // Do we really need to memset this, shouldn't it be enough to just do *bufToLm=0; ? /leif
  memset(bufToLm, '\0', 1024);

  // Intialize request as non-interactive
  ink_strncpy(bufToLm, "b ", bufRemaining);
  --bufRemaining;

  // Following options must be sent over as their numeric
  // equivalents that show up in the interactive menu
  // this is ugly but makes the number of handled events smaller
  // on the manager side
  if (reRead == 1) {
    // Handle reRead
    if (*readVar != '\0' || *setVar != '\0' || *varValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read or set values with re-read\n", programName);
      cli->disconnectFromLM();
      exit(1);
    } else {
      strncat(bufToLm, "3", bufRemaining);   /* reread */
      --bufRemaining;
    }
  } else if (ShutdownMgmtCluster == 1) {
    strncat(bufToLm, "9", bufRemaining);     /* restart_cluster */
    --bufRemaining;
  } else if (ShutdownMgmtLocal == 1) {
    strncat(bufToLm, "8", bufRemaining);     /* restart_local */
    --bufRemaining;
  } else if (Shutdown == 1) {
    strncat(bufToLm, "4", bufRemaining);     /* shutdown */
    --bufRemaining;
  } else if (BounceCluster == 1) {
    strncat(bufToLm, "7", bufRemaining);     /* bounce_cluster */
    --bufRemaining;
  } else if (BounceLocal == 1) {
    strncat(bufToLm, "6", bufRemaining);     /* bounce_local */
    --bufRemaining;
  } else if (Startup == 1) {
    strncat(bufToLm, "5", bufRemaining);     /* startup */
    --bufRemaining;
  } else if (ClearCluster == 1) {
    strncat(bufToLm, "10", bufRemaining);    /* clear_cluster */
    bufRemaining -= 2;
  } else if (ClearNode == 1) {
    strncat(bufToLm, "11", bufRemaining);    /* clear_node */
    bufRemaining -= 2;
  } else if (QueryDeadhosts == 1) {
    printResponse = true;
    strncat(bufToLm, "query_deadhosts", bufRemaining);
    bufRemaining -= 15;
  } else if (*readVar != '\0') {        // Handle a value read
    if (*setVar != '\0' || *varValue != '\0') {
      fprintf(stderr, "%s: Invalid Argument Combination: Can not read and set values at the same time\n", programName);
      cli->disconnectFromLM();
      exit(1);
    } else {
      printResponse = true;
      strncat(bufToLm, "get ", bufRemaining);
      bufRemaining -= 4;
      strncat(bufToLm, readVar, bufRemaining);
      bufRemaining -= strlen(readVar);
    }
  } else if (*setVar != '\0') { // Setting a variable
    if (*varValue == '\0') {
      fprintf(stderr, "%s: Set requires a -v argument\n", programName);
      cli->disconnectFromLM();
      exit(1);
    } else {
      strncat(bufToLm, "set ", bufRemaining);
      bufRemaining -= 4;
      strncat(bufToLm, setVar, bufRemaining);
      bufRemaining -= strlen(setVar);
      strncat(bufToLm, " ", bufRemaining);
      --bufRemaining;
      strncat(bufToLm, varValue, bufRemaining);
      bufRemaining -= strlen(varValue);
    }
  } else if (*varValue != '\0') {       // We have a value but no variable to set
    fprintf(stderr, "%s: Must specify variable to set with -s when using -v\n", programName);
    cli->disconnectFromLM();
    exit(1);
  } else if (timeout_arg > 0) { //INKqa10515
    timeLeft = timeout_arg * 1000;
  } else {
    fprintf(stderr, "%s: No arguments specified\n", programName);
    cli->disconnectFromLM();
    exit(1);
  }

  if (timeout_arg < 0) {
    timeLeft = -1;
  }

  cli->sendCommand(bufToLm, &response, timeLeft);
  responseStr = response.bufPtr();

  // parse response request from server into 3 tokens:
  // status, prompt and response
  respTok.setMaxTokens(3);      //<status>;<prompt>;<response>
  respTok.Initialize(response.bufPtr(), COPY_TOKS);
  status = respTok[0];
  resp = respTok[2];

  if (status && *status == '1') {       // OK
    // Only print responses for read, ignore prompt
    if (printResponse)
      printf("%s\n", resp ? resp : "NULL");
  } else if (responseStr && *responseStr == '0') {      // Error, Always print out errors
    fprintf(stderr, "%s: %s\n", programName, resp);
  } else {
    fprintf(stderr, "%s: Internal Error: Server Returned Invalid Reponse\n", programName);
    cli->disconnectFromLM();
    exit(1);
  }

  cli->disconnectFromLM();
}                               // end handleArgInvocation

static void
runInteractive(clientCLI * cli)
{
  char buf[512];                // holds request from interactive prompt
  char sendbuf[512];            // holds request to send LM
  textBuffer response(8192);    // holds response from Local Manager
  ink_hrtime timeLeft;
  Tokenizer respTok(";");
  const char *prompt = NULL;
  const char *resp = NULL;

  respTok.setMaxTokens(3);      //<status>;<prompt>;<response>

  // process input from command line
  while (1) {
    // Display a prompt
    if (prompt) {
      printf("%s", prompt);
    } else {                    // default
      printf("cli-> ");
    }

    // get input from command line
    NOWARN_UNUSED_RETURN(fgets(buf, 512, stdin));

    // check status of 'stdin' after reading
    if (feof(stdin) != 0) {
      fprintf(stderr, "%s: Detected EOF on input %s\n", programName, strerror(errno));
      cli->disconnectFromLM();
      exit(0);
    } else if (ferror(stdin) != 0) {
      fprintf(stderr, "%s: Error on reading user command: %s\n", programName, strerror(errno));
      exit(1);
    }
    // continue on newline
    if (strcmp(buf, "\n") == 0) {
      continue;
    }

    if (timeout_arg > 0) {
      timeLeft = timeout_arg * 1000;
    } else {
      timeLeft = -1;
    }

    // set interactive mode
    ink_strncpy(sendbuf, "i ", sizeof(sendbuf));
    strncat(sendbuf, buf, strlen(buf) - 1);     // do not send newline

    cli->sendCommand(sendbuf, &response, timeLeft);

    // exiting/quitting?
    if (strcasecmp("quit\n", buf) == 0 || strcasecmp("exit\n", buf) == 0) {
      // Don't wait for response LM
      cli->disconnectFromLM();
      exit(0);
    }
    // parse response request from server into 3 tokens:
    // status, prompt and response (but status is not used)
    respTok.Initialize(response.bufPtr(), COPY_TOKS);
    prompt = respTok[1];
    resp = respTok[2];

    // print response
    if (resp) {
      printf("%s\n", resp);
    }
    // reuse response buffer?
    response.reUse();

  }                             // end while(1)
}                               // end runInteractive

AppVersionInfo appVersionInfo;
int version_flag = 0;
/*
 * Main entry point
 */
int
main(int argc, char **argv)
{
  programName = argv[0];

  interactiveMode = 0;
  readVar[0] = '\0';
  setVar[0] = '\0';
  varValue[0] = '\0';
  reRead = 0;
  Shutdown = 0;
  BounceCluster = 0;
  BounceLocal = 0;
  QueryDeadhosts = 0;
  Startup = 0;
  ShutdownMgmtCluster = 0;
  ShutdownMgmtLocal = 0;
  ClearCluster = 0;
  ClearNode = 0;
  timeout_arg = -1;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,"traffic_line", PACKAGE_VERSION, __DATE__,
                       __TIME__, BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();
  clientCLI *cli = new clientCLI();

/* Argument description table used to describe how to parse command line args, */
/* see 'ink_args.h' for meanings of the various fields */
  ArgumentDescription argument_descriptions[] = {
#ifndef RNI_ONLY
    // INKqa10516
    /*{ "interactive", 'i', "Interactive Mode", "F", &interactiveMode, "CLI_INTERACTIVE", NULL}, */
#endif /* RNI_ONLY */
#ifndef _WIN32
    {"query_deadhosts", 'q', "Query congested sites", "F", &QueryDeadhosts, NULL, NULL},
    {"socket_path", 'p', "Socket Path", "S255", &cli->sockPath, "CLI_SOCKPATH", NULL},
#else
    {"cli_port", 'p', "Port Number", "I", &cli->cliPort, "CLI_PORT", NULL},
#endif
    {"read_var", 'r', "Read Variable", "S255", &readVar, NULL, NULL},
    {"set_var", 's', "Set Variable (requires -v option)", "S255", &setVar, NULL, NULL},
    {"value", 'v', "Set Value (used with -s option)", "S255", &varValue, NULL, NULL},
    {"help", 'h', "Help", NULL, NULL, NULL, usage},
    {"reread_config", 'x', "Reread Config Files", "F", &reRead, NULL, NULL},
    {"restart_cluster", 'M', "Restart traffic_manager (cluster wide)",
     "F", &ShutdownMgmtCluster, NULL, NULL},
    {"restart_local", 'L', "Restart traffic_manager (local node)",
     "F", &ShutdownMgmtLocal, NULL, NULL},
    {"shutdown", 'S', "Shutdown traffic_server (local node)", "F", &Shutdown, NULL, NULL},
    {"startup", 'U', "Start traffic_server (local node)", "F", &Startup, NULL, NULL},
    {"bounce_cluster", 'B', "Bounce traffic_server (cluster wide)", "F", &BounceCluster, NULL, NULL},
    {"bounce_local", 'b', "Bounce local traffic_server", "F", &BounceLocal, NULL, NULL},
    {"clear_cluster", 'C', "Clear Statistics (cluster wide)", "F", &ClearCluster, NULL, NULL},
    {"clear_node", 'c', "Clear Statistics (local node)", "F", &ClearNode, NULL, NULL},
    {"version", 'V', "Print Version Id", "T", &version_flag, NULL, NULL},
    /* INKqa10624
       { "timeout", 'T', "Request timeout (seconds)", "I", &timeout_arg, NULL, NULL} */

  };

  int n_argument_descriptions = SIZE(argument_descriptions);
  NOWARN_UNUSED(argc);

  // Process command line arguments and dump into variables
  process_args(argument_descriptions, n_argument_descriptions, argv);

  // check for the version number request
  if (version_flag) {
    ink_fputln(stderr, appVersionInfo.FullVersionInfoStr);
    exit(0);
  }

#ifdef _WIN32
  if (initWinSock() == false) {
    fprintf(stderr, "%s: unable to initialize winsock.\n", programName);
    exit(1);
  }
#endif

  // Connect to Local Manager
#ifndef _WIN32
  if (cli->connectToLM() != clientCLI::err_none) {
    char sock_path[PATH_NAME_MAX + 1];

    Layout::relative_to(sock_path, sizeof(sock_path),
                        Layout::get()->runtimedir, clientCLI::defaultSockPath);
    cli->setSockPath(sock_path);
    if (cli->connectToLM() != clientCLI::err_none) {
      fprintf(stderr, "%s: unable to connect to traffic_manager via %s\n", programName, cli->sockPath);
      exit(1);
    }
  }
#else
  if (cli->connectToLM() != clientCLI::err_none) {
    fprintf(stderr, "%s: unable to connect to traffic_manager via %s\n", programName, cli->sockPath);
    exit(1);
  }
#endif

#ifndef RNI_ONLY
  // Interactive or batch mode
  if (interactiveMode == 1) {
    runInteractive(cli);
  } else {
    handleArgInvocation(cli);
  }
#else /* RNI_ONLY */
  /* Interactive mode not allowed */
  handleArgInvocation(cli);
#endif /* RNI_ONLY */

  delete cli;

  return 0;
}                               // end main()
