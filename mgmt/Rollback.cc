/** @file

  This file contains code for class to allow rollback of configuration files

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

#include "tscore/ink_platform.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_time.h"
#include "Alarms.h"
#include "LocalManager.h"
#include "Rollback.h"
#include "WebMgmtUtils.h"
#include "ExpandingArray.h"
#include "MgmtSocket.h"
#include "tscore/I_Layout.h"
#include "FileManager.h"
#include "ProxyConfig.h"

#define MAX_VERSION_DIGITS 11
#define DEFAULT_BACKUPS 2

constexpr int ACTIVE_VERSION  = 0;
constexpr int INVALID_VERSION = -1;

#if HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtimespec.tv_nsec)
#elif HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000 + (t).st_mtim.tv_nsec)
#else
#define TS_ARCHIVE_STAT_MTIME(t) ((t).st_mtime * 1000000000)
#endif

// Error Strings
const char *RollbackStrings[] = {"Rollback Ok", "File was not found", "Version was out of date", "System Call Error",
                                 "Invalid Version - Version Numbers Must Increase"};

Rollback::Rollback(const char *fileName_, const char *configName_, bool root_access_needed_, Rollback *parentRollback_,
                   unsigned flags)
  : configFiles(nullptr),
    root_access_needed(root_access_needed_),
    parentRollback(parentRollback_),
    currentVersion(0),
    fileLastModified(0),
    numVersions(0)
{
  version_t highestSeen;             // the highest backup version
  ExpandingArray existVer(25, true); // Existing versions
  struct stat fileInfo;
  ink_assert(fileName_ != nullptr);

  // parent must not also have a parent
  if (parentRollback) {
    ink_assert(parentRollback->parentRollback == nullptr);
  }

  // Copy the file name.
  fileNameLen = strlen(fileName_);
  fileName    = ats_strdup(fileName_);
  configName  = ats_strdup(configName_);

  // Extract the file base name.
  fileBaseName = strrchr(fileName, '/');
  if (fileBaseName) {
    fileBaseName++;
  } else {
    fileBaseName = fileName;
  }

  ink_mutex_init(&fileAccessLock);

  // ToDo: This was really broken before, it  used to check if numberBackups <=0, but that could never happen.
  if (flags & CONFIG_FLAG_UNVERSIONED) {
    currentVersion = 0;
    setLastModifiedTime();
    return;
  }

  currentVersion = 0; // Prevent UMR with stat file
  highestSeen    = 0;

  // Check to make sure that our configuration file exists
  //
  if (statFile(ACTIVE_VERSION, &fileInfo) < 0) {
    // If we can't find an active version because there is none we have a hard failure.
    mgmt_fatal(0, "[RollBack::Rollback] Unable to find configuration file %s.\n\tStat failed : %s\n", fileName, strerror(errno));

  } else {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
    currentVersion   = highestSeen + 1;

    Debug("rollback", "[Rollback::Rollback] Current Version of %s is %d", fileName, currentVersion);
  }
}

Rollback::~Rollback()
{
  ats_free(fileName);
}

// Rollback::createPathStr(version_t version)
//
//   CALLEE DELETES STORAGE
//
char *
Rollback::createPathStr(version_t version)
{
  int bufSize  = 0;
  char *buffer = nullptr;
  std::string sysconfdir(RecConfigReadConfigDir());
  bufSize = sysconfdir.size() + fileNameLen + MAX_VERSION_DIGITS + 1;
  buffer  = (char *)ats_malloc(bufSize);
  Layout::get()->relative_to(buffer, bufSize, sysconfdir, fileName);
  if (version != ACTIVE_VERSION) {
    size_t pos = strlen(buffer);
    snprintf(buffer + pos, bufSize - pos, "_%d", version);
  }

  return buffer;
}

//
//
// int Rollback::statFile(version_t)
//
//  A wrapper for stat()
//
int
Rollback::statFile(version_t version, struct stat *buf)
{
  int statResult;

  if (version == this->currentVersion) {
    version = ACTIVE_VERSION;
  }

  ats_scoped_str filePath(createPathStr(version));

  statResult = root_access_needed ? elevating_stat(filePath, buf) : stat(filePath, buf);

  return statResult;
}

//
// int Rollback::openFile(version_t)
//
//  a wrapper for open()
//
int
Rollback::openFile(version_t version, int oflags, int *errnoPtr)
{
  int fd;

  ats_scoped_str filePath(createPathStr(version));
  // TODO: Use the original permissions
  //       Anyhow the _1 files should not be created inside Syconfdir.
  //
  fd = mgmt_open_mode_elevate(filePath, oflags, 0644, root_access_needed);

  if (fd < 0) {
    if (errnoPtr != nullptr) {
      *errnoPtr = errno;
    }
    mgmt_log("[Rollback::openFile] Open of %s failed: %s\n", fileName, strerror(errno));
  } else {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }

  return fd;
}

int
Rollback::closeFile(int fd, bool callSync)
{
  int result = 0;
  if (callSync && fsync(fd) < 0) {
    result = -1;
    mgmt_log("[Rollback::closeFile] fsync failed for file '%s' (%d: %s)\n", fileName, errno, strerror(errno));
  }

  if (result == 0) {
    result = close(fd);
  } else {
    close(fd);
  }
  return result;
}

// Rollback::internalUpdate()
//
//  Creates a version from buf.  Callee must be holding the lock
//
RollBackCodes
Rollback::internalUpdate(TextBuffer *buf, version_t newVersion, bool notifyChange, bool incVersion)
{
  RollBackCodes returnCode;
  char *activeVersion;
  char *currentVersion_local;
  char *nextVersion;
  ssize_t writeBytes;
  int diskFD;
  int ret;
  versionInfo *newBak;
  bool failedLink = false;
  char *alarmMsg  = nullptr;

  // Check to see if the callee has specified a newVersion number
  //   If the newVersion argument is less than zero, the callee
  //   is telling us to use the next version in sequence
  if (newVersion < 0) {
    newVersion = this->currentVersion + 1;
    if (incVersion) {
      incVersion = false; // because the version already increment
    }
  } else {
    // We need to make sure that the specified version is valid
    //  We can NOT go back in time to a smaller version number
    //  than the one we have now
    if (newVersion <= this->currentVersion) {
      return INVALID_VERSION_ROLLBACK;
    }
  }

  Debug("rollback", "[Rollback::internalUpdate] Moving %s from version %d to version %d", this->fileName, this->currentVersion,
        newVersion);

  currentVersion_local = createPathStr(this->currentVersion);
  activeVersion        = createPathStr(ACTIVE_VERSION);
  nextVersion          = createPathStr(newVersion);
  // Create the new configuration file
  // TODO: Make sure they are not created in Sysconfigdir!
  diskFD = openFile(newVersion, O_WRONLY | O_CREAT | O_TRUNC);

  if (diskFD < 0) {
    // Could not create the new file.  The operation is aborted
    mgmt_log("[Rollback::internalUpdate] Unable to create new version of %s : %s\n", fileName, strerror(errno));
    returnCode = SYS_CALL_ERROR_ROLLBACK;
    goto UPDATE_CLEANUP;
  }
  // Write the buffer into the new configuration file
  writeBytes = write(diskFD, buf->bufPtr(), buf->spaceUsed());
  ret        = closeFile(diskFD, true);
  if ((ret < 0) || ((size_t)writeBytes != buf->spaceUsed())) {
    mgmt_log("[Rollback::internalUpdate] Unable to write new version of %s : %s\n", fileName, strerror(errno));
    returnCode = SYS_CALL_ERROR_ROLLBACK;
    goto UPDATE_CLEANUP;
  }

  // Now that we got a the new version on the disk lets do some renaming
  if (link(activeVersion, currentVersion_local) < 0) {
    mgmt_log("[Rollback::internalUpdate] Link failed : %s\n", strerror(errno));

    // If the file was lost, it is lost and log the error and
    //    install a new file so that we do not go around in
    //    an endless loop
    if (errno == ENOENT) {
      mgmt_log("[Rollback::internalUpdate] The active version of %s was lost.\n\tThe updated copy was installed.\n", fileName);
      failedLink = true;
    } else {
      returnCode = SYS_CALL_ERROR_ROLLBACK;
      goto UPDATE_CLEANUP;
    }
  }

  if (rename(nextVersion, activeVersion) < 0) {
    mgmt_log("[Rollback::internalUpdate] Rename failed : %s\n", strerror(errno));
    mgmt_log("[Rollback::internalUpdate] Unable to create new version of %s.  Using prior version\n", fileName);

    returnCode = SYS_CALL_ERROR_ROLLBACK;
    goto UPDATE_CLEANUP;
  }

  setLastModifiedTime();

  // ToDo: We used to call removeVersion_ml() here.

  // If we created a backup version add it to the
  //  List of backup Versions
  if (failedLink == false) {
    newBak          = new versionInfo;
    newBak->version = this->currentVersion;
    newBak->modTime = 0;
    versionQ.enqueue(newBak);
  }
  // Update instance variables
  this->numVersions++;
  this->currentVersion = newVersion;

  returnCode = OK_ROLLBACK;

  // Post the change to the config file manager
  if (notifyChange && configFiles) {
    configFiles->fileChanged(fileName, configName, incVersion);
  }

UPDATE_CLEANUP:

  // Signal an alarm if we failed since if we are unable
  //   to manipulate the disk, the error might not get
  //   written to disk
  if (returnCode != OK_ROLLBACK) {
    alarmMsg = (char *)ats_malloc(1024);
    snprintf(alarmMsg, 1024, "[TrafficManager] Configuration File Update Failed: %s", strerror(errno));
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_CONFIG_UPDATE_FAILED, alarmMsg);
    ats_free(alarmMsg);

    // Remove both the link from currentVersion_local
    // to the active version and the new version
    //  that they will not screw up our version id on restart
    unlink(currentVersion_local);
    unlink(nextVersion);
  }

  ats_free(currentVersion_local);
  ats_free(activeVersion);
  ats_free(nextVersion);

  return returnCode;
}

RollBackCodes
Rollback::getVersion(version_t version, TextBuffer **buffer)
{
  RollBackCodes r;

  this->acquireLock();
  r = getVersion_ml(version, buffer);
  this->releaseLock();

  return r;
}

// CALLEE DELETES STORAGE
RollBackCodes
Rollback::getVersion_ml(version_t version, TextBuffer **buffer)
{
  int diskFD;               // file descriptor for version of the file we are fetching
  RollBackCodes returnCode; // our eventual return value
  struct stat fileInfo;     // Info from fstat
  int readResult;           // return val of (indirect) read calls
  TextBuffer *newBuffer;    // return buffer

  *buffer = nullptr;

  if (version == currentVersion) {
    version = ACTIVE_VERSION;
  }
  diskFD = openFile(version, O_RDONLY);

  if (diskFD < 0) {
    returnCode = FILE_NOT_FOUND_ROLLBACK;
    goto GET_CLEANUP;
  }
  // fstat the file so that we know what size is supposed to be
  if (fstat(diskFD, &fileInfo) < 0) {
    mgmt_log("[Rollback::getVersion] fstat on %s version %d failed: %s\n", fileName, version, strerror(errno));
    returnCode = FILE_NOT_FOUND_ROLLBACK;
    goto GET_CLEANUP;
  }
  // Create a textbuffer of the
  newBuffer = new TextBuffer(fileInfo.st_size + 1);

  do {
    readResult = newBuffer->readFromFD(diskFD);

    if (readResult < 0) {
      mgmt_log("[Rollback::getVersion] read failed on %s version %d: %s\n", fileName, version, strerror(errno));
      returnCode = SYS_CALL_ERROR_ROLLBACK;
      delete newBuffer;
      goto GET_CLEANUP;
    }
  } while (readResult > 0);

  if ((off_t)newBuffer->spaceUsed() != fileInfo.st_size) {
    mgmt_log("[Rollback::getVersion] Incorrect amount of data retrieved from %s version %d.  Expected: %d   Got: %d\n", fileName,
             version, fileInfo.st_size, newBuffer->spaceUsed());
    returnCode = SYS_CALL_ERROR_ROLLBACK;
    delete newBuffer;
    goto GET_CLEANUP;
  }

  *buffer = newBuffer;

  returnCode = OK_ROLLBACK;

GET_CLEANUP:

  if (diskFD >= 0) {
    closeFile(diskFD, false);
  }

  return returnCode;
}

// version_t Rollback::extractVersionInfo(ExpandingArray* listNames,
//                                        const char* testFileName)
//
//   Extracts the version number out of testFileName if it matches
//     with the fileName_version format; adds the fileInfo to
//     listNames if there is a match; returns INVALID_VERSION
//     if there is no match.
//
version_t
Rollback::extractVersionInfo(ExpandingArray *listNames, const char *testFileName)
{
  const char *str;
  version_t version = INVALID_VERSION;

  // Check to see if the current entry is a rollback file
  //   fileFormat: fileName_version
  //
  // Check to see if the prefix of the current entry
  //  is the same as our fileName
  if (strlen(testFileName) > fileNameLen) {
    if (strncmp(testFileName, fileName, fileNameLen) == 0) {
      // Check for the underscore
      if (*(testFileName + fileNameLen) == '_') {
        // Check for the integer version number
        const char *currentVersionStr = str = testFileName + fileNameLen + 1;

        for (; isdigit(*str) && *str != '\0'; str++) {
          ;
        }

        // Do not tolerate anything but numbers on the end
        //   of the file
        if (*str == '\0') {
          version = atoi(currentVersionStr);

          // Add info about version number and modTime
          if (listNames != nullptr) {
            struct stat fileInfo;

            if (statFile(version, &fileInfo) >= 0) {
              versionInfo *verInfo = (versionInfo *)ats_malloc(sizeof(versionInfo));
              verInfo->version     = version;
              verInfo->modTime     = fileInfo.st_mtime;
              listNames->addEntry((void *)verInfo);
            }
          }
        }
      }
    }
  }

  return version;
}

time_t
Rollback::versionTimeStamp(version_t version)
{
  time_t t;

  ink_mutex_acquire(&fileAccessLock);
  t = versionTimeStamp_ml(version);
  ink_mutex_release(&fileAccessLock);

  return t;
}

time_t
Rollback::versionTimeStamp_ml(version_t version)
{
  struct stat buf;

  if (this->statFile(version, &buf) < 0) {
    return -1;
  } else {
    return buf.st_mtime;
  }
}

int
Rollback::statVersion(version_t version, struct stat *buf)
{
  int r;

  ink_mutex_acquire(&fileAccessLock);
  r = this->statFile(version, buf);
  ink_mutex_release(&fileAccessLock);

  return r;
}

bool
Rollback::setLastModifiedTime()
{
  struct stat fileInfo;

  // Now we need to get the modification time off of the new active file
  if (statFile(ACTIVE_VERSION, &fileInfo) >= 0) {
    fileLastModified = TS_ARCHIVE_STAT_MTIME(fileInfo);
    return true;
  } else {
    // We really shouldn't fail to stat the file since we just
    //  created it.  If we do, just punt and just use the current
    //  time.
    fileLastModified = (time(nullptr) - ink_timezone()) * 1000000000;
    return false;
  }
}

// bool Rollback::checkForUserUpdate()
//
//  Called to check if the file has been changed  by the user.
//  Timestamps are compared to see if a change occurred
bool
Rollback::checkForUserUpdate()
{
  struct stat fileInfo;
  bool result;

  ink_mutex_acquire(&fileAccessLock);

  if (this->statFile(ACTIVE_VERSION, &fileInfo) < 0) {
    ink_mutex_release(&fileAccessLock);
    return false;
  }

  if (fileLastModified < TS_ARCHIVE_STAT_MTIME(fileInfo)) {
    setLastModifiedTime();
    configFiles->fileChanged(fileName, configName, true);
    mgmt_log("User has changed config file %s\n", fileName);

    result = true;
  } else {
    result = false;
  }

  ink_mutex_release(&fileAccessLock);
  return result;
}
