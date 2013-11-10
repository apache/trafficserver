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

/* hashtable.h
 * has function declarations for the stuff realted to hashtable
 *
 */

#ifndef CREATE_HASH
#define CREATE_HASH

#include "createArgument.h"

extern int cliCreateCommandHashtable();

extern int cliAddCommandtoHashtable(const char *name, cli_ArgvInfo * argtable, char **reqd_args,
                                    cli_parsedArgInfo * parsedArgTable, const char *helpString);

extern cli_CommandInfo *cliGetCommandArgsfromHashtable(char *name);

extern int cliParseArgument(int argc, const char **argv, cli_CommandInfo * commandinfo);

extern int getIntRange(char *range, int *r1, int *r2);
extern int floatIntRange(char *range, float *r1, float *r2);
#endif /* CREATE_HASH */
