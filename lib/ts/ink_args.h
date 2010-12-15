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

#ifndef _INK_ARGS_H
#define _INK_ARGS_H
#include "ink_defs.h"

#define MAX_FILE_ARGUMENTS 100

struct ArgumentDescription;

typedef void ArgumentFunction(ArgumentDescription * argument_descriptions, int n_argument_descriptions, const char *arg);

struct ArgumentDescription
{
  const char *name;
  char key;
  /*
     "I" = integer
     "L" = int64_t
     "D" = double (floating point)
     "T" = toggle
     "F" = set flag to TRUE (default is FALSE)
     "f" = set flag to FALSE (default is TRUE)
     "T" = toggle
     "S80" = read string, 80 chars max
   */
  const char *description;
  const char *type;
  void *location;
  const char *env;
  ArgumentFunction *pfn;
};

/* Global Data
*/
extern char *file_arguments[];  // exported by process_args()
extern int n_file_arguments;    // exported by process_args()
extern char *program_name;      // exported by process_args()

/* Print out arguments and values
*/
void show_argument_configuration(ArgumentDescription * argument_descriptions, int n_argument_descriptions);

void usage(ArgumentDescription * argument_descriptions, int n_argument_descriptions, const char *arg_unused);

/* Process all arguments
*/
void process_args(ArgumentDescription * argument_descriptions,
                  int n_argument_descriptions, char **argv, const char *usage_string = 0);

#endif /*_INK_ARGS_H*/
