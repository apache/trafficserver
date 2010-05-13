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

/* This file contains implementation of a class SafeShell  */
/* This safe shell, when started allows the user to only   */
/* execute a set of UNIX commands which are deemed to be   */
/* "SAFE" by the safe shell. The SafeShell class provides  */
/* an API to add or delete the list of safe shell          */


#define SAFESHELL_MAX_COMMANDS    256
#define SAFESHELL_CMD_LENGTH      256
#define SAFESHELL_PROMPT_STRING   "sfsh>"
#define SAFESHELL_DEBUG           0

#include "ink_hash_table.h"
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#define DEF_CMD_SIZE  4
char *default_cmds[] = {
  "ping",
  "netstat",
  "traceroute",
  "ls"
};




class SafeShell
{
  InkHashTable *safeCommandsTable;
public:
    SafeShell();
   ~SafeShell();
  int AddSafeShellCommand(char *);      /* Add a command name to the list of safe commands */
  int Run();                    /* Start running safe shell                        */
};


SafeShell::~SafeShell()
{
  ink_hash_table_destroy(safeCommandsTable);
}

SafeShell::SafeShell()
{
  safeCommandsTable = ink_hash_table_create(InkHashTableKeyType_String);
}

int
SafeShell::AddSafeShellCommand(char *command)
{
  int val = -1;
  if (command) {
    // If the last character is a newline, get rid of it.. Be smart
    if (command[strlen(command) - 1] == '\n') {
      command[strlen(command) - 1] = '\0';
    }
#if (SAFESHELL_DEBUG==1)
    printf("Inserting %s into the safeCommandTable\n", command);
#endif

    ink_hash_table_insert(safeCommandsTable, (InkHashTableKey) command, (InkHashTableValue) command);
  }
  return val;
}


/* This method will prompt the user and then wait untill the user hits "newline" and then */
/* execute the command which user entered if the command is in the list of Safe Commands  */
/* PS: This function returns if the user enters EOF                                       */
int
SafeShell::Run()
{
  int val = -1;
  for (;;) {
    char Command[SAFESHELL_CMD_LENGTH];
    fprintf(stdout, "%s ", SAFESHELL_PROMPT_STRING);
    char *val = NULL;
    if ((val = fgets(Command, SAFESHELL_CMD_LENGTH, stdin)) != NULL) {

      if (feof(stdin)) {
        fprintf(stdout, "\n");
        return EOF;
      }
      // Get rid of the newline
      Command[strlen(Command) - 1] = '\0';
      char Command2[SAFESHELL_CMD_LENGTH];
      char *commandname = NULL;
      // something was read
      strncpy(Command2, Command, SAFESHELL_CMD_LENGTH);
      // Get the first word out of it
      commandname = strtok(Command2, " ");

      if (commandname) {

        if (!strcasecmp(commandname, "exit") || !strcasecmp(commandname, "quit")) {
          fprintf(stdout, "\n");
          return EOF;
        }
        InkHashTableEntry *ht_entry = NULL;
#if (SAFESHELL_DEBUG==1)
        printf("Looking up  %s in the safeCommandTable\n", commandname);
#endif
        ht_entry = ink_hash_table_lookup_entry(safeCommandsTable, (InkHashTableKey) commandname);
        if (ht_entry) {
          system(Command);
        } else {
          printf("%s : command not found \n", commandname);
        }
      }
    }
    if (feof(stdin)) {
      fprintf(stdout, "\n");
      return EOF;
    }

  }
  return val;
}


#define SAFESHELL_CONFIG_FILE "etc/trafficserver/.sfshrc"

// The main driver for the SafeShell program. It reads in a config dil
int
main(int argc, char *argv[])
{

  FILE *fp = NULL;
  char *configfile = (char *) (argc == 3 ? argv[2] : SAFESHELL_CONFIG_FILE);
  char Commands[SAFESHELL_CMD_LENGTH];

  SafeShell safeShell;

  if ((fp = fopen(configfile, "r")) == NULL) {

    // If the file cannot be opened, default to the def_commands     ;
    for (int i = 0; i < DEF_CMD_SIZE; i++) {
      safeShell.AddSafeShellCommand(default_cmds[i]);
    }
  } else {

    // Otherwise we have the file open and can read the list of commands from there
    for (int i = 0; i < SAFESHELL_MAX_COMMANDS; i++) {
      if (fgets(Commands, SAFESHELL_MAX_COMMANDS, fp) == NULL) {
        break;
      }

      if (feof(fp))
        break;
      safeShell.AddSafeShellCommand(Commands);
    }
  }


  // Finally, go into the SafeShell loop
  safeShell.Run();
}
