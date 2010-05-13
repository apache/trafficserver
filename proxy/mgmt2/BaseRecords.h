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

/**************************************
 *
 * BaseRecords.h
 *   Base Records Class. Provides basic storage of Records, inherited
 * by other Record classes, who implmenet update methods dependent on
 * whether they are a supplier or consumer of the data.
 *
 *
 */

#ifndef _BASE_RECORDS_H
#define _BASE_RECORDS_H

#include "MgmtDBM.h"
#include "MgmtDefs.h"
#include "MgmtHashTable.h"
#include "TextBuffer.h"

#include "ink_mutex.h"


typedef void *(*RecordUpdateFunc) (void *opaque_token, void *data);
typedef void *(*RecordChangeFunc) (void *opaque_token, void *data);

typedef enum
{
  UPDATE_LOCK_ACQUIRE = 0,
  UPDATE_LOCK_RELEASE
} UpdateLockAction;
typedef void *(*UpdateLockFunc) (UpdateLockAction action);

/*
 * RecordType
 *   A list of the types of records currently used.
 */
typedef enum
{
  CONFIG = 0, PROCESS = 1, NODE = 2, CLUSTER = 3, LOCAL = 4, PLUGIN = 5,
  MAX_RECORD_TYPE = 7
} RecordType;

#define MAX_PLUGIN_RECORDS 100

/*
 * RecordID
 *   A unique identifier per record for fast access and indexing into
 * the record data structures. Used by id map hash tables.
 */
typedef struct _record_id
{
  int index;
  RecordType rtype;
} RecordID;


typedef struct _base_record_callback_list
{
  RecordChangeFunc func;
  void *opaque_token;
  struct _base_record_callback_list *next;
} CallBackList;

typedef struct _base_record
{

  int id;                       /* Integer identifier */
  RecordType rtype;             /* Type of Record */
  char *name;                   /* String name */
  MgmtType stype;               /* Type (counter, int, float, string) */

  bool changed;                 /* For update flushing */
  RecordUpdateFunc func;        /* Update callback */
  void *opaque_token;           /* Token registered */
  CallBackList *list;           /* For functions change notifications */

  bool read;                    /* Flag to denote read at least once */

  union
  {
    MgmtIntCounter counter_data;        /* Data */
    MgmtInt int_data;
    MgmtLLong llong_data;
    MgmtFloat float_data;
    MgmtString string_data;
  } data;

} Record;
void destroyRecord(Record * to_destroy);

typedef struct _base_records
{
  int num_recs;
  Record *recs;
} Records;
void destroyRecords(Records to_destroy);

class BaseRecords
{

public:

  BaseRecords(char *mpath, const char *cfile, char *efile = NULL);
   ~BaseRecords();

  void defineRecords();
  int rereadRecordFile(char *path, char *f, bool dirty_record_contents = true);

  bool registerUpdateFunc(int id, RecordType type, RecordUpdateFunc func, void *odata = NULL);
  bool registerChangeFunc(int id, RecordType type, RecordChangeFunc func, void *odata = NULL);
  bool unregisterChangeFunc(int id, RecordType type, RecordChangeFunc func, void *odata = NULL);

  Record *getRecord(int id, RecordType rtype);

  MgmtIntCounter incrementCounter(int id, RecordType type);
  MgmtIntCounter setCounter(int id, RecordType type, MgmtIntCounter value);
  MgmtInt setInteger(int id, RecordType type, MgmtInt value, bool dirty = true);
  MgmtLLong setLLong(int id, RecordType type, MgmtLLong value, bool dirty = true);
  MgmtFloat setFloat(int id, RecordType type, MgmtFloat value, bool dirty = true);
  bool setString(int id, RecordType type, MgmtString value, bool dirty = true);

  MgmtIntCounter incrementCounter(const char *name);
  MgmtIntCounter setCounter(const char *name, MgmtIntCounter value);
  MgmtInt setInteger(const char *name, MgmtInt value, bool dirty = true);
  MgmtLLong setLLong(const char *name, MgmtLLong value, bool dirty = true);
  MgmtFloat setFloat(const char *name, MgmtFloat value, bool dirty = true);
  bool setString(const char *name, MgmtString value, bool dirty = true);

  MgmtIntCounter readCounter(int id, RecordType type, bool * found = NULL);
  MgmtInt readInteger(int id, RecordType type, bool * found = NULL);
  MgmtLLong readLLong(int id, RecordType type, bool * found = NULL);
  MgmtFloat readFloat(int id, RecordType type, bool * found = NULL);
  MgmtString readString(int id, RecordType type, bool * found = NULL);

  MgmtIntCounter readCounter(const char *name, bool * found = NULL);
  MgmtInt readInteger(const char *name, bool * found = NULL);
  MgmtLLong readLLong(const char *name, bool * found = NULL);
  MgmtFloat readFloat(const char *name, bool * found = NULL);
  MgmtString readString(const char *name, bool * found = NULL);

  /*
   * Special interface, requires lock(rl_). Use with care!
   *    Created for use during change callbacks, in case you need to know the
   *    "current" value of another record in order to perform a reconfig.
   */
  MgmtIntCounter rl_readCounter(int id, RecordType type, bool * found = NULL);
  MgmtInt rl_readInteger(int id, RecordType type, bool * found = NULL);
  MgmtLLong rl_readLLong(int id, RecordType type, bool * found = NULL);
  MgmtFloat rl_readFloat(int id, RecordType type, bool * found = NULL);
  MgmtString rl_readString(int id, RecordType type, bool * found = NULL);

  MgmtIntCounter rl_readCounter(const char *name, bool * found = NULL);
  MgmtInt rl_readInteger(const char *name, bool * found = NULL);
  MgmtLLong rl_readLLong(const char *name, bool * found = NULL);
  MgmtFloat rl_readFloat(const char *name, bool * found = NULL);
  MgmtString rl_readString(const char *name, bool * found = NULL);

  MgmtIntCounter readCounter(const char *name, Records * recs, bool * found = NULL);
  MgmtInt readInteger(const char *name, Records * recs, bool * found = NULL);
  MgmtLLong readLLong(const char *name, Records * recs, bool * found = NULL);
  MgmtFloat readFloat(const char *name, Records * recs, bool * found = NULL);
  MgmtString readString(const char *name, Records * recs, bool * found = NULL);

  bool isvalidRecord(int id, RecordType type);
  MgmtType typeofRecord(int id, RecordType type);
  char *nameofRecord(int id, RecordType type);
  bool idofRecord(const char *name, int *id, RecordType * type);
  bool id_typeofRecord(const char *name, int *id, RecordType * type, MgmtType * mgmt_type);

  void updateRecord(Record * rec);
  void updateRecords(RecordType type);
  void notifyChangeLists(RecordType type, bool no_reset = false);

  bool syncPutRecord(Record * rec, char *pref = NULL, bool force_flush = false);
  bool syncPutRecords(RecordType type, char *pref = NULL, bool force_flush = false);

  bool syncGetRecord(Record * rec, char *pref = NULL, bool ignore = false);
  void syncGetRecords(RecordType type, char *pref = NULL, bool ignore = false);

  bool getExternalRecordValue(Record * rec, char *p);
  void removeExternalRecords(RecordType type, long p = -1);

  textBuffer *createRecordsFile(char *fname);
  void printRecord(Record rec);
  void printRecords(RecordType type);
  void printRecords();
  void dumpReadRegisterReport();

  void clearRecords(RecordType type);

  int getUpdateCount(RecordType type);

  void registerUpdateLockFunc(UpdateLockFunc func);

  /*
   * addPlugin*(...)
   *   Functions for adding plugin defined variables.
   *
   * Returns:    true if sucessful
   *             false otherwise
   */
  bool addPluginCounter(const char *name, MgmtIntCounter value);
  bool addPluginInteger(const char *name, MgmtInt value);
  bool addPluginLLong(const char *name, MgmtLLong value);
  bool addPluginFloat(const char *name, MgmtFloat value);
  bool addPluginString(const char *name, MgmtString value);

  /*
   * Convenience function for the addPlugin*(...) functions
   */
  Record *addPluginRecord(const char *name, MgmtType stype);

  long pid;
  char str_pid[1024];
  MgmtDBM *record_db;
  char config_file[PATH_NAME_MAX + 1];

  MgmtHashTable *record_files;
  MgmtHashTable *record_id_map;
  MgmtHashTable *record_type_map;

  MgmtHashTable *user_modified_configs_ht;
  ink_mutex record_textbuffer_lock;

  UpdateLockFunc f_update_lock;

  ink_mutex mutex[MAX_RECORD_TYPE];
  int updateCount[MAX_RECORD_TYPE];

  Records config_data;
  Records process_data;
  Records node_data;
  Records cluster_data;
  Records local_data;
  Records plugin_data;

private:

};                              /* End class BaseRecords */

#endif /* _BASE_RECORDS_H */
