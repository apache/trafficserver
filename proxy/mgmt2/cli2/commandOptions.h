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

/* commandOptions.h
 * these are valid options for createArgument
 *
 *
 */

#ifndef COMMAND_OPTIONS
#define COMMAND_OPTIONS


#define CLI_ARGV_CONSTANT_OPTIONAL                    0x401
#define CLI_ARGV_INT_OPTIONAL                         0x402
#define CLI_ARGV_STRING_OPTIONAL                      0x404
#define CLI_ARGV_FLOAT_OPTIONAL                       0x408
#define CLI_ARGV_FUNC_OPTIONAL                        0x410
#define CLI_ARGV_HELP_OPTIONAL                        0x420
#define CLI_ARGV_CONST_OPTION_OPTIONAL                0x440
#define CLI_ARGV_CONSTANT_REQUIRED                    0x801
#define CLI_ARGV_INT_REQUIRED                         0x802
#define CLI_ARGV_STRING_REQUIRED                      0x804
#define CLI_ARGV_FLOAT_REQUIRED                       0x808


#endif /*COMMAND_OPTIONS */
