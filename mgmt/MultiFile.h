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

#ifndef _MULTI_FILE_H_
#define _MULTI_FILE_H_

/****************************************************************************
 *
 *  MultiFile.h - base class to handle reading and displaying config
 *                 files and directories
 *
 *
 ****************************************************************************/

class ExpandingArray;
class TextBuffer;

#if defined(NAME_MAX)
#define FILE_NAME_MAX NAME_MAX
#else
#define FILE_NAME_MAX 255
#endif

struct fileEntry {
  // XXX Remove this arbitrary filename length limit.
  char name[FILE_NAME_MAX];
  time_t c_time;
};

enum MFresult {
  MF_OK,
  MF_NO_DIR,
};

// class MultiFile
//
//  The purpose of this class is to allow Snapshots and
//    Autoconfig to share common code for reading directories
//    and displaying editing information
//
//  Locking issues are up to the child class
//  No Virtual Functions.  There is not a reason to for anyone to
//    use MultiFile* so I'm saving the overhead of virtual tables
//
class MultiFile
{
public:
  MultiFile();

protected:
  MFresult WalkFiles(ExpandingArray *fileList);
  void addTableEntries(ExpandingArray *fileList, TextBuffer *output);
  char *newPathString(const char *s1, const char *s2);
  bool isManaged(const char *fileName);
  void addSelectOptions(TextBuffer *output, ExpandingArray *options);
  char *managedDir;
  const char *dirDescript;
};

int fileEntryCmpFunc(const void *e1, const void *e2);

#endif
