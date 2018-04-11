/** @file

  Private RecFile declarations

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

#pragma once

//-------------------------------------------------------------------------
// types/defines
//-------------------------------------------------------------------------

#define REC_HANDLE_INVALID -1
typedef int RecHandle;

//-------------------------------------------------------------------------
// RecFile
//-------------------------------------------------------------------------

RecHandle RecFileOpenR(const char *file);
RecHandle RecFileOpenW(const char *file);
int RecFileClose(RecHandle h_file);
int RecFileRead(RecHandle h_file, char *buf, int size, int *bytes_read);
int RecFileWrite(RecHandle h_file, char *buf, int size, int *bytes_written);
int RecFileGetSize(RecHandle h_file);
int RecFileExists(const char *file);
int RecFileSync(RecHandle h_file);
