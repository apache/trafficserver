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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"
#include "ts/ink_defs.h"
#include "ts/ink_assert.h"
#include "ts/ink_error.h"
#include "ts/ink_file.h"
#include "ts/ink_string.h"
#include "ts/ink_time.h"

#include "MgmtUtils.h"
#include "MultiFile.h"
#include "ExpandingArray.h"
#include "ts/TextBuffer.h"
#include "WebMgmtUtils.h"

/****************************************************************************
 *
 *  MultiFile.cc - base class to handle reading and displaying config
 *                 files and directories
 *
 *
 ****************************************************************************/

MultiFile::MultiFile()
{
  managedDir  = NULL;
  dirDescript = NULL;
}

// void MultiFile::addTableEntries(ExpandingArray* fileList, textBuffer* output)
//
//   Adds table entries to output from the result of WalkFiles
//
void
MultiFile::addTableEntries(ExpandingArray *fileList, textBuffer *output)
{
  int numFiles = fileList->getNumEntries();
  fileEntry *current;
  char *safeName;
  char dateBuf[64];
  const char dataOpen[]  = "\t<td>";
  const char dataClose[] = "</td>\n";
  const int dataOpenLen  = strlen(dataOpen);
  const int dataCloseLen = strlen(dataClose);

  for (int i = 0; i < numFiles; i++) {
    current = (fileEntry *)((*fileList)[i]);

    output->copyFrom("<tr>\n", 5);
    output->copyFrom(dataOpen, dataOpenLen);
    safeName = substituteForHTMLChars(current->name);
    output->copyFrom(safeName, strlen(safeName));
    delete[] safeName;
    output->copyFrom(dataClose, dataCloseLen);
    output->copyFrom(dataOpen, dataOpenLen);

    if (ink_ctime_r(&current->c_time, dateBuf) == NULL) {
      ink_strlcpy(dateBuf, "<em>No time-stamp</em>", sizeof(dateBuf));
    }
    output->copyFrom(dateBuf, strlen(dateBuf));
    output->copyFrom(dataClose, dataCloseLen);
    output->copyFrom("</tr>\n", 6);
  }
}

// Mfresult MultiFile::WalkFiles(ExpandingArray* fileList)
//
//   Iterates through the managed directory and adds every managed file
//     into the parameter snapList
//

MFresult
MultiFile::WalkFiles(ExpandingArray *fileList)
{
  struct dirent *dirEntry;
  DIR *dir;
  char *fileName;
  char *filePath;
  char *records_config_filePath;
  struct stat fileInfo;
  struct stat records_config_fileInfo;
  fileEntry *fileListEntry;

  if ((dir = opendir(managedDir)) == NULL) {
    mgmt_log(stderr, "[MultiFile::WalkFiles] Unable to open %s directory: %s: %s\n", dirDescript, managedDir, strerror(errno));
    return MF_NO_DIR;
  }

  while ((dirEntry = readdir(dir))) {
    fileName                = dirEntry->d_name;
    filePath                = newPathString(managedDir, fileName);
    records_config_filePath = newPathString(filePath, "records.config");

    if (stat(filePath, &fileInfo) < 0) {
      mgmt_log(stderr, "[MultiFile::WalkFiles] Stat of a %s failed %s: %s\n", dirDescript, fileName, strerror(errno));
    } else {
      if (stat(records_config_filePath, &records_config_fileInfo) < 0) {
        delete[] filePath;
        delete[] records_config_filePath;
        continue;
      }
      // Ignore ., .., and any dot files
      if (*fileName != '.' && isManaged(fileName)) {
        fileListEntry         = (fileEntry *)ats_malloc(sizeof(fileEntry));
        fileListEntry->c_time = fileInfo.st_ctime;
        ink_strlcpy(fileListEntry->name, fileName, sizeof(fileListEntry->name));
        fileList->addEntry(fileListEntry);
      }
    }

    delete[] filePath;
    delete[] records_config_filePath;
  }

  closedir(dir);

  fileList->sortWithFunction(fileEntryCmpFunc);
  return MF_OK;
}

bool
MultiFile::isManaged(const char *fileName)
{
  if (fileName == NULL) {
    return false;
  } else {
    return true;
  }
}

void
MultiFile::addSelectOptions(textBuffer *output, ExpandingArray *options)
{
  const char selectEnd[]  = "</select>\n";
  const char option[]     = "\t<option value='";
  const int optionLen     = strlen(option);
  const char option_end[] = "'>";
  char *safeCurrent;

  int numOptions = options->getNumEntries();

  for (int i = 0; i < numOptions; i++) {
    output->copyFrom(option, optionLen);
    safeCurrent = substituteForHTMLChars((char *)((*options)[i]));
    output->copyFrom(safeCurrent, strlen(safeCurrent));
    output->copyFrom(option_end, strlen(option_end));
    output->copyFrom(safeCurrent, strlen(safeCurrent));
    delete[] safeCurrent;
    output->copyFrom("\n", 1);
  }
  output->copyFrom(selectEnd, strlen(selectEnd));
}

//  int fileEntryCmpFunc(void* e1, void* e2)
//
//  a cmp function for fileEntry structs that can
//     used with qsort
//
//  compares c_time
//
int
fileEntryCmpFunc(const void *e1, const void *e2)
{
  fileEntry *entry1 = (fileEntry *)*(void **)e1;
  fileEntry *entry2 = (fileEntry *)*(void **)e2;

  if (entry1->c_time > entry2->c_time) {
    return 1;
  } else if (entry1->c_time < entry2->c_time) {
    return -1;
  } else {
    return 0;
  }
}

// char* MultiFile::newPathString(const char* s1, const char* s2)
//
//   creates a new string that is composed of s1/s2
//     Callee is responsible for deleting storage
//     Method makes sure there is no double slash between s1 and s2
//     The code is borrowed from ink_filepath_make with dynamic allocation.
//
char *
MultiFile::newPathString(const char *s1, const char *s2)
{
  char *newStr;
  int srcLen; // is the length of the src rootpath
  int addLen; // maximum total path length

  // Treat null as an empty path.
  if (!s2)
    s2   = "";
  addLen = strlen(s2) + 1;
  if (*s2 == '/') {
    // If addpath is rooted, then rootpath is unused.
    newStr = new char[addLen];
    ink_strlcpy(newStr, s2, addLen);
    return newStr;
  }
  if (!s1 || !*s1) {
    // If there's no rootpath return the addpath
    newStr = new char[addLen];
    ink_strlcpy(newStr, s2, addLen);
    return newStr;
  }
  srcLen = strlen(s1);
  newStr = new char[srcLen + addLen + 1];
  ink_assert(newStr != NULL);

  ink_strlcpy(newStr, s1, addLen);
  if (newStr[srcLen - 1] != '/')
    newStr[srcLen++] = '/';
  ink_strlcpy(&newStr[srcLen], s2, addLen - srcLen);

  return newStr;
}
