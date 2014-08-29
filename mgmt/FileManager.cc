/** @file

  Code for class to manage configuration updates

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

#include "libts.h"
#include "I_Layout.h"
#include "FileManager.h"
#include "Main.h"
#include "Rollback.h"
#include "WebMgmtUtils.h"
#include "MgmtUtils.h"
#include "WebGlobals.h"
#include "ExpandingArray.h"
#include "MgmtSocket.h"

#define DIR_MODE S_IRWXU
#define FILE_MODE S_IRWXU

typedef fileEntry snapshot;

const char *SnapshotStrings[] = { "Request Successful\n",
  "No Snapshot Directory",
  "Snapshot was not found\n",
  "Creation of snapshot directory failed\n",
  "Creation of snapshot file failed\n",
  "Access to snapshot file Failed\n",
  "Unable to write to snapshot file\n",
  "Remove of Snapshot failed\n",
  "Internal Error: Form Submission was invalid\n",
  "No Snapshot Name Was Given\n",
  "Invalid Snapshot name\n"
};

FileManager::FileManager()
{
  bindings = ink_hash_table_create(InkHashTableKeyType_String);
  ink_assert(bindings != NULL);

  ink_mutex_init(&accessLock, "File Manager Mutex");
  ink_mutex_init(&cbListLock, "File Changed Callback Mutex");

  ats_scoped_str snapshotDir(RecConfigReadSnapshotDir());

  // Check to see if the directory already exists, if not create it.
  if (access(snapshotDir, F_OK) == -1) {
    if (mkdir(snapshotDir, DIR_MODE) < 0) {
      // Failed to create the snapshot directory
      mgmt_fatal(stderr, 0, "[FileManager::FileManager] Failed to create the snapshot directory %s: %s\n", (const char *)snapshotDir, strerror(errno));
    }
  }

  this->managedDir = snapshotDir.release();
  this->dirDescript = "snapshot";
}

// FileManager::~FileManager
//
//  There is only FileManager object in the process and it
//     should never need to be destructed except at
//     program exit
//
FileManager::~FileManager()
{

  callbackListable *cb;
  fileBinding *bind;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  // Let other operations finish and do not start any new ones
  ink_mutex_acquire(&accessLock);

  ats_free(this->managedDir);
  this->managedDir = NULL;
  this->dirDescript = NULL;

  for (cb = cblist.pop(); cb != NULL; cb = cblist.pop()) {
    delete cb;
  }

  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {

    bind = (fileBinding *) ink_hash_table_entry_value(bindings, entry);

    delete bind->rb;
    delete bind;
  }

  ink_hash_table_destroy(bindings);


  ink_mutex_destroy(&accessLock);
  ink_mutex_destroy(&cbListLock);
}


// void FileManager::registerCallback(FileCallbackFunc func)
//
//  Adds a new callback function
//    callbacks are made whenever a configuration file has
//    changed
//
//  The callback function is responsible for free'ing
//    the string the string it is passed
//
void
FileManager::registerCallback(FileCallbackFunc func)
{

  callbackListable *newcb = new callbackListable();
  ink_assert(newcb != NULL);
  newcb->func = func;
  ink_mutex_acquire(&cbListLock);
  cblist.push(newcb);
  ink_mutex_release(&cbListLock);
}

// void FileManager::addFile(char* baseFileName, const configFileInfo* file_info)
//
//  for the baseFile, creates a Rollback object for it
//
//  if file_info is not null, a WebFileEdit object is also created for
//    the file
//
//  Pointers to the new objects are stored in the bindings hashtable
//
void
FileManager::addFile(const char *baseFileName, bool root_access_needed)
{

  ink_assert(baseFileName != NULL);
  fileBinding *newBind = new fileBinding;

  newBind->rb = new Rollback(baseFileName, root_access_needed);
  newBind->rb->configFiles = this;

  ink_mutex_acquire(&accessLock);
  ink_hash_table_insert(bindings, baseFileName, newBind);
  ink_mutex_release(&accessLock);
}

// bool FileManager::getRollbackObj(char* baseFileName, Rollback** rbPtr)
//
//  Sets rbPtr to the rollback object associated
//    with the passed in baseFileName.
//
//  If there is no binding, falseis returned
//
bool
FileManager::getRollbackObj(const char *baseFileName, Rollback ** rbPtr)
{

  InkHashTableValue lookup;
  fileBinding *bind;
  int found;

  ink_mutex_acquire(&accessLock);
  found = ink_hash_table_lookup(bindings, baseFileName, &lookup);
  ink_mutex_release(&accessLock);

  bind = (fileBinding *) lookup;
  if (found == 0) {
    return false;
  } else {
    *rbPtr = bind->rb;
    return true;
  }
}

// bool FileManager::fileChanged(const char* baseFileName)
//
//  Called by the Rollback class whenever a a config has changed
//     Initiates callbacks
//
//
void
FileManager::fileChanged(const char *baseFileName, bool incVersion)
{

  callbackListable *cb;
  char *filenameCopy;

  ink_mutex_acquire(&cbListLock);


  for (cb = cblist.head; cb != NULL; cb = cb->link.next) {
    // Dup the string for each callback to be
    //  defensive incase it modified when it is not supposed to be
    filenameCopy = ats_strdup(baseFileName);
    (*cb->func) (filenameCopy, incVersion);
    ats_free(filenameCopy);
  }
  ink_mutex_release(&cbListLock);
}

// textBuffer* FileManager::filesManaged()
//
//  Returns a comma separated list of all files currently being
//    managed by this object
//
//  CALLEE DELETES the returned space
textBuffer *
FileManager::filesManaged()
{

  textBuffer *result = new textBuffer(1024);
  const char *currentName;
  const char separator[] = "\n";
  fileBinding *bind;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  ink_mutex_acquire(&accessLock);
  // To get a stable snap shot, we need to get the rollback
  //   locks on all configuration files so the files
  //   do not change from under us
  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {

    bind = (fileBinding *) ink_hash_table_entry_value(bindings, entry);
    currentName = bind->rb->getBaseName();
    ink_assert(currentName);

    result->copyFrom(currentName, strlen(currentName));
    result->copyFrom(separator, 1);
  }
  ink_mutex_release(&accessLock);

  return result;
}

// void FileManager::doRollbackLocks(lockAction_t action)
//
//  Iterates through the Rollback objects we are managing
//    and performs the parameter specified action on each lock
//
//  CALLLEE must hold this->accessLock
//
void
FileManager::doRollbackLocks(lockAction_t action)
{

  fileBinding *bind;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {

    bind = (fileBinding *) ink_hash_table_entry_value(bindings, entry);

    switch (action) {
    case ACQUIRE_LOCK:
      bind->rb->acquireLock();
      break;
    case RELEASE_LOCK:
      bind->rb->releaseLock();
      break;
    default:
      ink_assert(0);
      break;
    }
  }
}


// void FileManager::abortRestore(const char* abortTo)
//
//  Iterates through the hash table of managed files
//    and Rollsback one version until we get to the file
//    named abortTo
//
//  It is a fatal error for any of the Rollbacks to
//    fail since that leaves the configuration in an
//    indeterminate state
//
//  CALLEE should be holding the locks on all the Rollback
//    objects
//
void
FileManager::abortRestore(const char *abortTo)
{

  fileBinding *bind;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  version_t currentVersion;

  ink_assert(abortTo != NULL);

  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {

    bind = (fileBinding *) ink_hash_table_entry_value(bindings, entry);

    // We are done
    if (strcmp(abortTo, bind->rb->getBaseName()) == 0) {
      return;
    }

    currentVersion = bind->rb->getCurrentVersion();
    if (bind->rb->revertToVersion_ml(currentVersion - 1) != OK_ROLLBACK) {
      mgmt_fatal(stderr, 0,
                 "[FileManager::abortRestore] Unable to abort a failed snapshot restore.  Configuration files have been left in a inconsistent state\n");
    }
  }
}


// SnapResult FileManager::restoresSnap(const char* snapName)
//
//  Restores the snapshot with snapName.
//
//  If restoration fails, calls this->abortRestore
//    to reset the configuraton state
//
SnapResult
FileManager::restoreSnap(const char *snapName, const char *snapDir)
{
  fileBinding *bind;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  SnapResult result = SNAP_OK;
  char *snapPath;
  char *filePath = NULL;
  textBuffer storage(2048);

  snapPath = newPathString(snapDir, snapName);

  ink_mutex_acquire(&accessLock);



  if (access(snapPath, F_OK) == -1) {
    delete[]snapPath;
    ink_mutex_release(&accessLock);
    return SNAP_NOT_FOUND;
  }

  // To get a stable restore, we need to get the rollback
  //   locks on all configuration files so that the active files
  //   do not change from under us
  doRollbackLocks(ACQUIRE_LOCK);

  // For each file, load the snap shot file and Roll a new version
  //    of the active file
  //
  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {

    bind = (fileBinding *) ink_hash_table_entry_value(bindings, entry);
    filePath = newPathString(snapPath, bind->rb->getBaseName());
    if (readFile(filePath, &storage) != SNAP_OK) {
      abortRestore(bind->rb->getBaseName());
      result = SNAP_FILE_ACCESS_FAILED;
      break;
    }

    if (bind->rb->forceUpdate_ml(&storage) != OK_ROLLBACK) {
      abortRestore(bind->rb->getBaseName());
      result = SNAP_FILE_ACCESS_FAILED;
      break;
    }
    delete[]filePath;
    filePath = NULL;
    storage.reUse();
  }

  doRollbackLocks(RELEASE_LOCK);
  ink_mutex_release(&accessLock);

  if (filePath != NULL) {
    delete[]filePath;
  }

  delete[]snapPath;
  return result;
}




// SnapResult FileManager::removeSnap(const char* snapName)
//
//
SnapResult
FileManager::removeSnap(const char *snapName, const char *snapDir)
{
  struct dirent *dirEntrySpace;
  struct dirent *entryPtr;
  DIR *dir;
  char *snapPath;
  char *snapFilePath;
  bool unlinkFailed = false;
  SnapResult result = SNAP_OK;
  snapPath = newPathString(snapDir, snapName);

  dir = opendir(snapPath);

  if (dir == NULL) {
    mgmt_log(stderr, "[FileManager::removeSnap] Unable to open snapshot %s: %s\n", snapName, strerror(errno));
    delete[]snapPath;
    return SNAP_NOT_FOUND;
  }

  dirEntrySpace = (struct dirent *)ats_malloc(sizeof(struct dirent) + pathconf(".", _PC_NAME_MAX) + 1);

  while (readdir_r(dir, dirEntrySpace, &entryPtr) == 0) {
    if (!entryPtr)
      break;

    if (strcmp(".", entryPtr->d_name) == 0 || strcmp("..", entryPtr->d_name) == 0) {
      continue;
    }

    snapFilePath = newPathString(snapPath, entryPtr->d_name);

    if (unlink(snapFilePath) < 0) {
      mgmt_log(stderr, "[FileManager::removeSnap] Unlink failed for %s: %s\n", snapFilePath, strerror(errno));
      unlinkFailed = true;
      result = SNAP_REMOVE_FAILED;
    }
    delete[]snapFilePath;
  }

  ats_free(dirEntrySpace);
  closedir(dir);

  // If we managed to get everything, remove the directory
  //
  if (unlinkFailed == false) {
    if (rmdir(snapPath) < 0) {
      // strerror() isn't reentrant/thread-safe ... Problem? /leif
      mgmt_log(stderr,
               "[FileManager::removeSnap] Unable to remove snapshot directory %s: %s\n", snapPath, strerror(errno));
      result = SNAP_REMOVE_FAILED;
    } else {
      result = SNAP_OK;
    }
  }

  delete[]snapPath;
  return result;
}









 //
 //  Creates a new snapshot with snapName
 //     Creates a directory named snapName in the snapshot directory
 //     Places a copy of every config file into the new directory
 //

SnapResult
FileManager::takeSnap(const char *snapName, const char *snapDir)
{

  fileBinding *bind;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;
  char *snapPath;
  SnapResult callResult = SNAP_OK;
  struct stat snapDirStat;


  // Make sure the user sent us a name
  if (snapName == NULL || *snapName == '\0') {
    return SNAP_NO_NAME_GIVEN;
  }

  if (strchr(snapName, '/') != NULL) {
    return SNAP_ILLEGAL_NAME;
  }
  // make sure the name is legal and cleaned up
  if (!checkValidName(snapName)) {
    return SNAP_ILLEGAL_NAME;
  }


  snapPath = newPathString(snapDir, snapName);

  if (!stat(snapPath, &snapDirStat)) {
    if (!S_ISDIR(snapDirStat.st_mode)) {
      delete[]snapPath;
      return SNAP_DIR_CREATE_FAILED;
    }
  }
  if (mkdir(snapPath, DIR_MODE) < 0) {
    mgmt_log(stderr, "[FileManager::takeSnap] Failed to create directory for snapshot %s: %s\n",
             snapName, strerror(errno));
    delete[]snapPath;
    return SNAP_DIR_CREATE_FAILED;
  }

  ink_mutex_acquire(&accessLock);


  // To get a stable snap shot, we need to get the rollback
  //   locks on all configuration files so the files
  //   do not change from under us
  doRollbackLocks(ACQUIRE_LOCK);

  // For each file, make a copy in the snap shot directory
  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {

    bind = (fileBinding *) ink_hash_table_entry_value(bindings, entry);
    callResult = this->copyFile(bind->rb, snapPath);
    if (callResult != SNAP_OK) {
      // Remove the failed napshot so that we do not have a partial
      //   one hanging around
      if (removeSnap(snapName, snapDir) != SNAP_OK) {
        mgmt_log(stderr,
                 "[FileManager::takeSnap] Unable to remove failed snapshot %s.  This snapshot should be removed by hand\n",
                 snapName);
      }
      break;
    }
  }

  // Free all the locks since we are done
  //
  doRollbackLocks(RELEASE_LOCK);

  ink_mutex_release(&accessLock);
  delete[]snapPath;
  return callResult;
}








//
//  SnapResult FileManager::readFile(const char* filePath, textBuffer* contents)
//
//  Reads the specified file into the textBuffer.  Returns SNAP_OK if
//    the file was successfully read and an error code otherwise
//
SnapResult
FileManager::readFile(const char *filePath, textBuffer * contents)
{
  int diskFD;
  int readResult;

  ink_assert(contents != NULL);
  diskFD = mgmt_open(filePath, O_RDONLY);

  if (diskFD < 0) {
    mgmt_log(stderr, "[FileManager::readFile] Open of snapshot file failed %s: %s\n", filePath, strerror(errno));
    return SNAP_FILE_ACCESS_FAILED;
  }

  fcntl(diskFD, F_SETFD, 1);

  while ((readResult = contents->readFromFD(diskFD)) > 0);
  close(diskFD);

  if (readResult < 0) {
    mgmt_log(stderr, "[FileManager::readFile] Read of snapshot file failed %s: %s\n", filePath, strerror(errno));
    return SNAP_FILE_ACCESS_FAILED;
  }

  return SNAP_OK;
}

//  SnapResult FileManager::copyFile(Rollback* rb, const char* snapPath)
//
//  Copies a file (represented by Rollback* rb) into a snapshot
//    directory (snapPath)
//
SnapResult
FileManager::copyFile(Rollback * rb, const char *snapPath)
{
  const char *fileName;
  char *filePath;
  int diskFD;
  textBuffer *copyBuf;
  SnapResult result;

  fileName = rb->getBaseName();

  // Load the current config into memory
  //
  // The Rollback lock is held by CALLEE
  if (rb->getVersion_ml(rb->getCurrentVersion(), &copyBuf) != OK_ROLLBACK) {
    mgmt_log(stderr, "[FileManager::copyFile] Unable to retrieve current version of %s\n", fileName);
    return SNAP_FILE_ACCESS_FAILED;
  }
  // Create the new file
  filePath = newPathString(snapPath, fileName);
  diskFD = mgmt_open_mode(filePath, O_RDWR | O_CREAT, FILE_MODE);

  if (diskFD < 0) {
    mgmt_log(stderr, "[FileManager::copyFile] Unable to create snapshot file %s: %s\n", fileName, strerror(errno));
    delete[]filePath;
    delete copyBuf;
    return SNAP_FILE_CREATE_FAILED;
  }

  fcntl(diskFD, F_SETFD, 1);

  // Write the file contents to the copy
  if (write(diskFD, copyBuf->bufPtr(), copyBuf->spaceUsed()) < 0) {
    mgmt_log(stderr, "[FileManager::copyFile] Unable to write snapshot file %s: %s\n", fileName, strerror(errno));
    result = SNAP_WRITE_FAILED;
  } else {
    result = SNAP_OK;
  }

  delete[]filePath;
  delete copyBuf;
  close(diskFD);
  return result;
}







// SnapResult FileManager::WalkSnaps(ExpandingArray* snapList)
//
//   Iterates through the snapshot directory and adds every snapshot
//     into the parameter snapList
//
//   CALLEE should be holding this->accessLock
//
SnapResult
FileManager::WalkSnaps(ExpandingArray * snapList)
{
  MFresult r;

  // The original code reset this->managedDir from proxy.config.snapshot_dir at this point. There doesn't appear to be
  // any need for that, since managedDir is always set in the constructor and should not be changed.
  ink_release_assert(this->managedDir != NULL);

  ink_mutex_acquire(&accessLock);

  r = WalkFiles(snapList);
  //lmgmt->record_data ->setString("proxy.config.snapshot_dir", managedDir);

  ink_mutex_release(&accessLock);
  return (SnapResult) r;
}



// void FileManger::rereadConfig()
//
//   Interates through the list of managed files and
//     calls Rollback::checkForUserUpdate on them
//
//   although it is tempting, DO NOT CALL FROM SIGNAL HANDLERS
//      This function is not Async-Signal Safe.  It
//      is thread safe
void
FileManager::rereadConfig()
{
  fileBinding *bind;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  ink_mutex_acquire(&accessLock);
  for (entry = ink_hash_table_iterator_first(bindings, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(bindings, &iterator_state)) {

    bind = (fileBinding *) ink_hash_table_entry_value(bindings, entry);
    bind->rb->checkForUserUpdate();
  }
  ink_mutex_release(&accessLock);

  // INKqa11910
  // need to first check that enable_customizations is enabled
  bool found;
  int enabled = (int) REC_readInteger("proxy.config.body_factory.enable_customizations",
                                      &found);
  if (found && enabled) {
    fileChanged("proxy.config.body_factory.template_sets_dir", true);
  }
}

// void FileManager::displaySnapPage(textBuffer* output, httpResponse& answerHdr)
//
//  Generates an HTML page with the add form and the list
//    of current snapshots
//
void
FileManager::displaySnapOption(textBuffer * output)
{
  ExpandingArray snap_list(25, true);
  SnapResult snap_result;
  int num_snaps;

  snap_result = WalkSnaps(&snap_list);
  if (snap_result == SNAP_OK) {
    num_snaps = snap_list.getNumEntries();
    if (num_snaps > 0) {
      addSelectOptions(output, &snap_list);
    }
  }
}



// void FileManger::createSelect(char* formVar, textBuffer* output, ExpandingArray*)
//
//  Creats a form with a select list.  The select options come
//    from the expanding array.  Action is the value for the hidden input
//    tag with name action
//
void
FileManager::createSelect(char *action, textBuffer * output, ExpandingArray * options)
{
  const char formOpen[] = "<form method=POST action=\"/configure/snap_action.html\">\n<select name=snap>\n";
  const char formEnd[] = "</form>";
  const char submitButton[] = "<input type=submit value=\"";
  const char hiddenInput[] = "<input type=hidden name=action value=";

  int numOptions;

  numOptions = options->getNumEntries();

  if (numOptions > 0) {
    output->copyFrom(formOpen, strlen(formOpen));
    addSelectOptions(output, options);
    output->copyFrom(hiddenInput, strlen(hiddenInput));
    output->copyFrom(action, strlen(action));
    output->copyFrom(">\n", 2);
    output->copyFrom(submitButton, strlen(submitButton));
    output->copyFrom(action, strlen(action));
    output->copyFrom("\">\n", 3);
    output->copyFrom(formEnd, strlen(formEnd));
  }
}


// bool checkValidName(const char* name)
//
// if the string is invalid, ie. all white spaces or contains "irregular" chars,
// returns 0 ; returns 1 if valid string
//
bool
FileManager::checkValidName(const char *name)
{
  int length = strlen(name);

  for (int i = 0; i < length; i++) {
    if (!isprint(name[i]))
      return false;             // invalid - unprintable char
    if (!isspace(name[i]))
      return true;              // has non-white space that is printable
  }

  return false;                 // all white spaces
}


//  int snapEntryCmpFunc(void* e1, void* e2)
//
//  a cmp function for snapshot structs that can
//     used with qsort
//
//  compares c_time
//
int
snapEntryCmpFunc(const void *e1, const void *e2)
{
  snapshot *entry1 = (snapshot *) * (void **) e1;
  snapshot *entry2 = (snapshot *) * (void **) e2;

  if (entry1->c_time > entry2->c_time) {
    return 1;
  } else if (entry1->c_time < entry2->c_time) {
    return -1;
  } else {
    return 0;
  }
}
