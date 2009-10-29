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

#include "ink_unused.h"      /* MAGIC_EDITING_TAG */
/*
 *
 * LMRecords.cc
 *   Member function definitions for the LMRecords class.
 *
 * $Date: 2008-05-20 17:26:20 $
 *
 * 
 */

#include "LMRecords.h"
#include "Main.h"

MgmtIntCounter
LMRecords::incrementCounter(int id, RecordType type)
{
  if (type == CONFIG) {
    time_t t;
    ink_mutex_acquire(&mutex[CONFIG]);
    if ((t = time(NULL)) > 0) {
      time_last_config_change = t;
    }
    ink_mutex_release(&mutex[CONFIG]);
  }
  return (BaseRecords::incrementCounter(id, type));
}                               /* End LMRecords::incrementCounter */

MgmtIntCounter
LMRecords::setCounter(int id, RecordType type, MgmtIntCounter value)
{
  if (type == CONFIG) {
    time_t t;
    ink_mutex_acquire(&mutex[CONFIG]);
    if ((t = time(NULL)) > 0) {
      time_last_config_change = t;
    }
    ink_mutex_release(&mutex[CONFIG]);
  }
  return (BaseRecords::setCounter(id, type, value));
}                               /* End LMRecords::setCounter */

MgmtInt
LMRecords::setInteger(int id, RecordType type, MgmtInt value)
{
  if (type == CONFIG) {
    time_t t;
    ink_mutex_acquire(&mutex[CONFIG]);
    if ((t = time(NULL)) > 0) {
      time_last_config_change = t;
    }
    ink_mutex_release(&mutex[CONFIG]);
  }
  update_user_defined_records(id, type);
  return (BaseRecords::setInteger(id, type, value));
}                               /* End LMRecords::setInteger */

MgmtLLong
LMRecords::setLLong(int id, RecordType type, MgmtLLong value)
{
  if (type == CONFIG) {
    time_t t;
    ink_mutex_acquire(&mutex[CONFIG]);
    if ((t = time(NULL)) > 0) {
      time_last_config_change = t;
    }
    ink_mutex_release(&mutex[CONFIG]);
  }
  update_user_defined_records(id, type);
  return (BaseRecords::setLLong(id, type, value));
}                               /* End LMRecords::setLLong */

MgmtFloat
LMRecords::setFloat(int id, RecordType type, MgmtFloat value)
{
  if (type == CONFIG) {
    time_t t;
    ink_mutex_acquire(&mutex[CONFIG]);
    if ((t = time(NULL)) > 0) {
      time_last_config_change = t;
    }
    ink_mutex_release(&mutex[CONFIG]);
  }
  update_user_defined_records(id, type);
  return (BaseRecords::setFloat(id, type, value));
}                               /* End LMRecords::setFloat */

bool
LMRecords::setString(int id, RecordType type, MgmtString value)
{
  if (type == CONFIG) {
    time_t t;
    ink_mutex_acquire(&mutex[CONFIG]);
    if ((t = time(NULL)) > 0) {
      time_last_config_change = t;
    }
    ink_mutex_release(&mutex[CONFIG]);
  }
  update_user_defined_records(id, type);
  return (BaseRecords::setString(id, type, value));
}                               /* End LMRecords::setString */

MgmtIntCounter
LMRecords::incrementCounter(const char *name)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return incrementCounter(id, type);
  }
  return INVALID;
}                               /* End LMRecords::incrementCounter */


MgmtIntCounter
LMRecords::setCounter(const char *name, MgmtIntCounter value)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setCounter(id, type, value);
  }
  return INVALID;
}                               /* End LMRecords::setCounter */


MgmtInt
LMRecords::setInteger(const char *name, MgmtInt value)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setInteger(id, type, value);
  }
  return INVALID;
}                               /* End LMRecords::setInteger */


MgmtLLong
LMRecords::setLLong(const char *name, MgmtLLong value)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setLLong(id, type, value);
  }
  return INVALID;
}                               /* End LMRecords::setLLong */


MgmtFloat
LMRecords::setFloat(const char *name, MgmtFloat value)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setFloat(id, type, value);
  }
  return INVALID;
}                               /* End LMRecords::setFloat */


bool
LMRecords::setString(const char *name, MgmtString value)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type)) {
    return setString(id, type, value);
  }
  return false;
}                               /* End LMRecords::setString */


MgmtIntCounter
LMRecords::readPProcessCounter(int id, RecordType type, char *p)
{
  MgmtIntCounter ret = INVALID;
  Record *rec;

  ink_mutex_acquire(&mutex[type]);
  rec = getRecord(id, type);
  if ((getExternalRecordValue(rec, p)) && rec->stype == INK_COUNTER) {
    ret = (rec->data.counter_data);
  }
  ink_mutex_release(&mutex[type]);

  return ret;
}                               /* End LMRecords::readPProcessCounter */


MgmtInt
LMRecords::readPProcessInteger(int id, RecordType type, char *p)
{
  MgmtInt ret = INVALID;
  Record *rec;

  ink_mutex_acquire(&mutex[type]);
  rec = getRecord(id, type);
  if ((getExternalRecordValue(rec, p)) && rec->stype == INK_INT) {
    ret = (rec->data.int_data);
  }
  ink_mutex_release(&mutex[type]);

  return ret;
}                               /* End LMRecords::readPProcessInteger */


MgmtLLong
LMRecords::readPProcessLLong(int id, RecordType type, char *p)
{
  MgmtLLong ret = INVALID;
  Record *rec;

  ink_mutex_acquire(&mutex[type]);
  rec = getRecord(id, type);
  if ((getExternalRecordValue(rec, p)) && rec->stype == INK_LLONG) {
    ret = (rec->data.llong_data);
  }
  ink_mutex_release(&mutex[type]);

  return ret;
}                               /* End LMRecords::readPProcessLLong */


MgmtFloat
LMRecords::readPProcessFloat(int id, RecordType type, char *p)
{
  MgmtFloat ret = INVALID;
  Record *rec;

  ink_mutex_acquire(&mutex[type]);
  rec = getRecord(id, type);
  if ((getExternalRecordValue(rec, p)) && rec->stype == INK_FLOAT) {
    ret = (rec->data.float_data);
  }
  ink_mutex_release(&mutex[type]);

  return ret;
}                               /* End LMRecords::readPProcessFloat */


MgmtString
LMRecords::readPProcessString(int id, RecordType type, char *p)
{
  MgmtString ret = NULL;
  Record *rec;

  ink_mutex_acquire(&mutex[type]);
  rec = getRecord(id, type);
  if ((getExternalRecordValue(rec, p)) && rec->stype == INK_STRING) {
    ink_assert((ret = (MgmtString) xmalloc(strlen(rec->data.string_data) + 1)));
    ink_strncpy(ret, rec->data.string_data, sizeof(ret));
  }
  ink_mutex_release(&mutex[type]);

  return ret;
}                               /* End LMRecords::readPProcessString */




bool
LMRecords::syncRecords(bool sync_get_records, bool forceProcessRecordsSnap)
{
  bool ret = false;

  if ((sync_get_records && lm->processRunning()) || forceProcessRecordsSnap) {

// Remove by elam, since we are not multi-process we dont need the prefix right now
//      char pref[1024];
//      snprintf(pref, sizeof(pref), "%ld", lm->watched_process_pid);
//      syncGetRecords(PROCESS, pref);

    syncGetRecords(PROCESS, NULL);
    syncGetRecords(PLUGIN, NULL);
  }
//    INKqa12757: Disabling the manager changes I did to fix INKqa06971
//    syncPutRecords(NODE, NULL);
//    syncPutRecords(CLUSTER, NULL);

  ret = syncPutRecords(LOCAL, NULL) || syncPutRecords(CONFIG, NULL);
  if (ret) {
    Rollback *rb;

    textBuffer *tmp = createRecordsFile(config_file);
    if (configFiles->getRollbackObj(config_file, &rb)) {
      version_t ver = rb->getCurrentVersion();
      if ((rb->updateVersion(tmp, ver)) != OK_ROLLBACK) {
        mgmt_elog(stderr, "[LMRecords::syncRecords] Record file updated failed: '%s'\n", config_file);
      }
    }
    delete tmp;

  }
  return ret;
}                               /* End LMRecords::syncRecords */

bool
LMRecords::registerConfigUpdateFunc(int id, RecordChangeFunc func, void *odata)
{
  return (registerChangeFunc(id, CONFIG, func, odata));
}                               /* End LMRecords::registerConfigUpdateFunc */


bool
LMRecords::registerConfigUpdateFunc(const char *name, RecordChangeFunc func, void *odata)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type) && type == CONFIG) {
    return (registerConfigUpdateFunc(id, func, odata));
  }
  return false;
}                               /* End LMRecords::registerConfigUpdateFunc */

void
LMRecords::update_user_defined_records(int id, RecordType type)
{

  // user modified config/local fields, make a note of it
  if (type == CONFIG || type == LOCAL) {
    ink_debug_assert(user_modified_configs_ht != 0);
    Record *rec = getRecord(id, type);
    ink_mutex_acquire(&mutex[type]);
    const size_t name_size = strlen(rec->name) + 1;
    char *name = (char *) xmalloc(name_size);
    ink_debug_assert(name);
    ink_strncpy(name, rec->name, name_size);
    if (!(user_modified_configs_ht->mgmt_hash_table_isbound(name))) {
      // add to our text buffer
      ink_mutex_acquire(&record_textbuffer_lock);
      textBuffer *buff = 0;
      record_files->mgmt_hash_table_lookup((InkHashTableKey) config_file, (void **) &buff);
      buff->copyFrom(name, strlen(name));
      buff->copyFrom("\n", strlen("\n"));
      ink_mutex_release(&record_textbuffer_lock);
      // add to our hash of what's in the text buffer
      user_modified_configs_ht->mgmt_hash_table_insert(name, name);
    } else {
      xfree(name);
    }
    ink_mutex_release(&mutex[type]);
  }

}
