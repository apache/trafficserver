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

#ifndef _AUTO_CONFIG_H_
#define _AUTO_CONFIG_H_

/****************************************************************************
 *
 *  AutoConf.h - code to generate and delete client autoconf files
 *
 *
 ****************************************************************************/

#include "MultiFile.h"
#include "ink_hash_table.h"

// MUST match the ordering MFresult so that we can cast
//   MFresult to PACresult
enum PACresult
{ PAC_OK, PAC_NO_DIR, PAC_CREATE_FAILED,
  PAC_INVALID_SUBMISSION, PAC_FILE_EXISTS,
  PAC_REMOVE_FAILED, PAC_MISSING_FILE_NAME
};

class textBuffer;
class Tokenizer;

// class AutoConf : public MultiFile {
class AutoConf
{
public:
  AutoConf();
  ~AutoConf();
  void processAction(char *submission, textBuffer * output);
  void displayAutoConfPage(textBuffer * output);
  void handleView(textBuffer * output, int flag);       // Moved from private->public to help handle
  // Bug INKqa00991 -GV
private:
    PACresult handleCreate(InkHashTable * params);
  PACresult handleRemove();
  void byPass(textBuffer & newFile, Tokenizer & tok, const char *funcStr);
  void addProxy(textBuffer & output, char *hostname, char *port, bool first, bool final);
  bool BuildFile(InkHashTable * parameters, textBuffer & newFile);
  void pacErrorResponse(const char *action, PACresult error, textBuffer * output);
};

extern AutoConf *autoConfObj;
#endif
