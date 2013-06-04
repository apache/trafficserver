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

/*
 *   createArgument.h --
 *
 *
 *    This file provides basic create Argument declartion,
 *    for any new arguments.
 *
 *
 *
 *
 *    Dated  : 12/11/2000.
 */

#ifndef CREATE_ARGUMENT
#define CREATE_ARGUMENT

#include <commandOptions.h>

typedef union arg_value
{                               /* Value of the argument can be
                                   integr or float or string */
  char *arg_string;
  int arg_int;
  float arg_float;
} arg_value;

typedef union lower_range
{
  int int_r1;
  float float_r1;
} lower_range;

typedef union upper_range
{
  int int_r2;
  float float_r2;
} upper_range;

typedef struct cli_ArgvInfo
{
  char *key;                    /*  The key string that flags
                                   the option  in the argv array */
  int position;                 /* indicates position of the
                                   argument in command */
  int type;                     /* Indicates argument type;  */
  int arg_ref;                  /* User creates an integer to
                                   refer to this argument */
  lower_range l_range;          /* lower_range */
  upper_range u_range;          /* upper_range */
  bool range_set;                /* flag which indicates if range is set by user */
  int option;                   /* flag which indicates if argument is optional or required */
  char *help;                   /* Documentation message describing this  option. */
  char *def;                    /* default value */

} cli_ArgvInfo;


typedef struct cli_parsedArgInfo
{
  int parsed_args;
  char *data;
  int arg_int;
  float arg_float;
  char *arg_string;
  char *arg_usage;
} cli_parsedArgInfo;


typedef struct cli_cmdCallbackInfo
{
  const char *command_usage;
  cli_parsedArgInfo *parsedArgTable;
  void *userdata;
} cli_cmdCallbackInfo;


typedef struct cli_CommandInfo
{
  const char *command_name;           /* command name    */
  cli_ArgvInfo *argtable;       /* pointer to argv table */
  char **reqd_args;             /* holds reference no for
                                   required arguments */
  cli_parsedArgInfo *parsedArgTable;    /* holds parsed arguments */
  char *helpString;

} cli_CommandInfo;

extern int createArgument(const char *argument, int position, int commandoption,
                          char *range, int argumentRef, const char *helpString, char *defValue);



extern int getIntRange(char *range, int *r1, int *r2);
extern int getFloatRange(char *range, float *r1, float *r2);

#endif /*CREATE_ARGUMENT */
