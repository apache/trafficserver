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

#pragma once
#include "tscore/ink_defs.h"
#include "tscore/ink_apidefs.h"

#if HAVE_SYSEXITS_H
#include <sysexits.h>
#endif

#ifndef EX_USAGE
#define EX_USAGE 64
#endif

#define MAX_FILE_ARGUMENTS 100

struct ArgumentDescription;
class AppVersionInfo;

typedef void ArgumentFunction(const ArgumentDescription *argument_descriptions, unsigned n_argument_descriptions, const char *arg);

struct ArgumentDescription {
  const char *name;
  char key; // set to '-' if no single character key.
            /*
               "I" = integer
               "L" = int64_t
               "D" = double (floating point)
               "T" = toggle
               "F" = set flag to TRUE (default is FALSE)
               "f" = set flag to FALSE (default is TRUE)
               "T" = toggle
               "S80" = read string, 80 chars max
               "S*" = read unbounded string, allocating
             */
  const char *description;
  const char *type;
  void *location;
  const char *env;
  ArgumentFunction *pfn;
};

#define VERSION_ARGUMENT_DESCRIPTION()                                         \
  {                                                                            \
    "version", 'V', "Print version string", nullptr, nullptr, nullptr, nullptr \
  }
#define HELP_ARGUMENT_DESCRIPTION()                                          \
  {                                                                          \
    "help", 'h', "Print usage information", nullptr, nullptr, nullptr, usage \
  }
#define RUNROOT_ARGUMENT_DESCRIPTION()                                                 \
  {                                                                                    \
    "run-root", '-', "using TS_RUNROOT as sandbox", nullptr, nullptr, nullptr, nullptr \
  }

/* Global Data
 */
extern const char *file_arguments[]; // exported by process_args()
extern unsigned n_file_arguments;    // exported by process_args()
extern const char *program_name;     // exported by process_args()
extern int cmd_disable_pfreelist;

/* Print out arguments and values
 */
void show_argument_configuration(const ArgumentDescription *argument_descriptions, unsigned n_argument_descriptions);

void usage(const ArgumentDescription *argument_descriptions, unsigned n_argument_descriptions, const char *arg_unused) TS_NORETURN;

/* Process all arguments
 */
void process_args(const AppVersionInfo *appinfo, const ArgumentDescription *argument_descriptions, unsigned n_argument_descriptions,
                  const char **argv, const char *usage_string = nullptr);

bool process_args_ex(const AppVersionInfo *appinfo, const ArgumentDescription *argument_descriptions,
                     unsigned n_argument_descriptions, const char **argv);
