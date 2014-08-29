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

/****************************************************************************
Process arguments

****************************************************************************/

#include "libts.h"

//
//  Global variables
//

const char *file_arguments[MAX_FILE_ARGUMENTS] = { 0 };
const char *program_name = (char *) "Traffic Server";
unsigned n_file_arguments = 0;

//
//  Local variables
//

static const char *argument_types_keys = (char *) "ISDfFTL";
static const char *argument_types_descriptions[] = {
  (char *) "int  ",
  (char *) "str  ",
  (char *) "dbl  ",
  (char *) "off  ",
  (char *) "on   ",
  (char *) "tog  ",
  (char *) "i64  ",
  (char *) "     "
};

//
// Functions
//

static void
process_arg(const ArgumentDescription * argument_descriptions,
            unsigned n_argument_descriptions, int i, char ***argv, const char *usage_string)
{
  char *arg = NULL;
  if (argument_descriptions[i].type) {
    char type = argument_descriptions[i].type[0];
    if (type == 'F' || type == 'f')
      *(int *) argument_descriptions[i].location = type == 'F' ? 1 : 0;
    else if (type == 'T')
      *(int *) argument_descriptions[i].location = !*(int *) argument_descriptions[i].location;
    else {
      arg = *++(**argv) ? **argv : *++(*argv);
      if (!arg)
        usage(argument_descriptions, n_argument_descriptions, usage_string);
      switch (type) {
      case 'I':
        *(int *) argument_descriptions[i].location = atoi(arg);
        break;
      case 'D':
        *(double *) argument_descriptions[i].location = atof(arg);
        break;
      case 'L':
        *(int64_t *) argument_descriptions[i].location = ink_atoi64(arg);
        break;
      case 'S':
        if (argument_descriptions[i].type[1] == '*') {
          char ** out = (char **)argument_descriptions[i].location;
          *out = ats_strdup(arg);
        } else {
          ink_strlcpy((char *) argument_descriptions[i].location, arg, atoi(argument_descriptions[i].type + 1));
        }
        break;
      default:
        ink_fatal(1, (char *) "bad argument description");
        break;
      }
      **argv += strlen(**argv) - 1;
    }
  }
  if (argument_descriptions[i].pfn)
    argument_descriptions[i].pfn(argument_descriptions, n_argument_descriptions, arg);
}


void
show_argument_configuration(const ArgumentDescription * argument_descriptions, unsigned n_argument_descriptions)
{
  printf("Argument Configuration\n");
  for (unsigned i = 0; i < n_argument_descriptions; i++) {
    if (argument_descriptions[i].type) {
      printf("  %-34s ", argument_descriptions[i].description);
      switch (argument_descriptions[i].type[0]) {
      case 'F':
      case 'f':
      case 'T':
        printf(*(int *) argument_descriptions[i].location ? "TRUE" : "FALSE");
        break;
      case 'I':
        printf("%d", *(int *) argument_descriptions[i].location);
        break;
      case 'D':
        printf("%f", *(double *) argument_descriptions[i].location);
        break;
      case 'L':
        printf("%" PRId64 "", *(int64_t *) argument_descriptions[i].location);
        break;
      case 'S':
        printf("%s", (char *) argument_descriptions[i].location);
        break;
      default:
        ink_fatal(1, (char *) "bad argument description");
        break;
      }
      printf("\n");
    }
  }
}

void
process_args(const ArgumentDescription * argument_descriptions, unsigned n_argument_descriptions, char **argv, const char *usage_string)
{
  unsigned i = 0;
  //
  // Grab Environment Variables
  //
  for (i = 0; i < n_argument_descriptions; i++)
    if (argument_descriptions[i].env) {
      char type = argument_descriptions[i].type[0];
      char *env = getenv(argument_descriptions[i].env);
      if (!env)
        continue;
      switch (type) {
      case 'f':
      case 'F':
      case 'I':
        *(int *) argument_descriptions[i].location = atoi(env);
        break;
      case 'D':
        *(double *) argument_descriptions[i].location = atof(env);
        break;
      case 'L':
        *(int64_t *) argument_descriptions[i].location = atoll(env);
        break;
      case 'S':
        ink_strlcpy((char *) argument_descriptions[i].location, env, atoi(argument_descriptions[i].type + 1));
        break;
      }
    }
  //
  // Grab Command Line Arguments
  //
  program_name = argv[0];
  while (*++argv) {
    if (**argv == '-') {
      if ((*argv)[1] == '-') {
        for (i = 0; i < n_argument_descriptions; i++)
          if (!strcmp(argument_descriptions[i].name, (*argv) + 2)) {
            *argv += strlen(*argv) - 1;
            process_arg(argument_descriptions, n_argument_descriptions, i, &argv, usage_string);
            break;
          }
        if (i >= n_argument_descriptions)
          usage(argument_descriptions, n_argument_descriptions, usage_string);
      } else {
        while (*++(*argv))
          for (i = 0; i < n_argument_descriptions; i++)
            if (argument_descriptions[i].key == **argv) {
              process_arg(argument_descriptions, n_argument_descriptions, i, &argv, usage_string);
              break;
            }
        if (i >= n_argument_descriptions)
          usage(argument_descriptions, n_argument_descriptions, usage_string);
      }
    } else {
      if (n_file_arguments >= countof(file_arguments)) {
        ink_fatal(1, "too many files");
      }
      file_arguments[n_file_arguments++] = *argv;
      file_arguments[n_file_arguments] = NULL;
    }
  }
}

void
usage(const ArgumentDescription * argument_descriptions, unsigned n_argument_descriptions, const char *usage_string)
{
  (void) argument_descriptions;
  (void) n_argument_descriptions;
  (void) usage_string;
  if (usage_string)
    fprintf(stderr, "%s\n", usage_string);
  else
    fprintf(stderr, "Usage: %s [--SWITCH [ARG]]\n", program_name);
  fprintf(stderr, "  switch__________________type__default___description\n");
  for (unsigned i = 0; i < n_argument_descriptions; i++) {
    if (!argument_descriptions[i].description)
      continue;

    fprintf(stderr, "  ");

    if ('-' == argument_descriptions[i].key) fprintf(stderr, "   ");
    else fprintf(stderr, "-%c,", argument_descriptions[i].key);
                                               
    fprintf(stderr, " --%-17s %s",
            argument_descriptions[i].name,
            argument_types_descriptions[argument_descriptions[i].type ?
                                        strchr(argument_types_keys,
                                               argument_descriptions[i].type[0]) -
                                        argument_types_keys : strlen(argument_types_keys)
            ]);
    switch (argument_descriptions[i].type ? argument_descriptions[i].type[0] : 0) {
    case 0:
      fprintf(stderr, "          ");
      break;
    case 'L':
      fprintf(stderr, " %-9" PRId64 "", *(int64_t *) argument_descriptions[i].location);
      break;
    case 'S':
      if (*(char *) argument_descriptions[i].location) {
        if (strlen((char *) argument_descriptions[i].location) < 10)
          fprintf(stderr, " %-9s", (char *) argument_descriptions[i].location);
        else {
          ((char *) argument_descriptions[i].location)[7] = 0;
          fprintf(stderr, " %-7s..", (char *) argument_descriptions[i].location);
        }
      } else
        fprintf(stderr, " (null)   ");
      break;
    case 'D':
      fprintf(stderr, " %-9.3f", *(double *) argument_descriptions[i].location);
      break;
    case 'I':
      fprintf(stderr, " %-9d", *(int *) argument_descriptions[i].location);
      break;
    case 'T':
    case 'f':
    case 'F':
      fprintf(stderr, " %-9s", *(int *) argument_descriptions[i].location ? "true " : "false");
      break;
    }
    fprintf(stderr, " %s\n", argument_descriptions[i].description);
  }
  _exit(1);
}
