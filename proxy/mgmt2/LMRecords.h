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
 *
 * LMRecords.h
 *   Class LM Records, record keeper for the local manager.
 *
 * $Date: 2006-03-08 19:40:19 $
 *
 *
 */

#ifndef _LM_RECORDS_H
#define _LM_RECORDS_H

#include "BaseRecords.h"

class LocalManager;

class LMRecords:public BaseRecords
{

public:

  LMRecords(char *mpath, const char *cfile, char *efile):BaseRecords(mpath, cfile, efile)
  {
    //char fpath[PATH_NAME_MAX];
    time_last_config_change = 0;
    //snprintf(fpath, sizeof(fpath), "%s%s%s", system_runtime_dir,DIR_SEP,MGMT_DB_FILENAME);
    //unlink(fpath);
  };
  ~LMRecords() {
  };

  MgmtIntCounter incrementCounter(int id, RecordType type);
  MgmtIntCounter setCounter(int id, RecordType type, MgmtIntCounter value);
  MgmtInt setInteger(int id, RecordType type, MgmtInt value);
  MgmtLLong setLLong(int id, RecordType type, MgmtLLong value);
  MgmtFloat setFloat(int id, RecordType type, MgmtFloat value);
  bool setString(int id, RecordType type, MgmtString value);

  MgmtIntCounter incrementCounter(const char *name);
  MgmtIntCounter setCounter(const char *name, MgmtIntCounter value);
  MgmtInt setInteger(const char *name, MgmtInt value);
  MgmtLLong setLLong(const char *name, MgmtLLong value);
  MgmtFloat setFloat(const char *name, MgmtFloat value);
  bool setString(const char *name, MgmtString value);

  MgmtIntCounter readPProcessCounter(int id, RecordType type, char *p);
  MgmtInt readPProcessInteger(int id, RecordType type, char *p);
  MgmtLLong readPProcessLLong(int id, RecordType type, char *p);
  MgmtFloat readPProcessFloat(int id, RecordType type, char *p);
  MgmtString readPProcessString(int id, RecordType type, char *p);

  bool registerConfigUpdateFunc(int id, RecordChangeFunc func, void *odata = NULL);
  bool registerConfigUpdateFunc(const char *name, RecordChangeFunc func, void *odata = NULL);

  bool syncRecords(bool sync_get_records = true, bool forceProcessRecordsSnap = false);

  LocalManager *lm;

  time_t time_last_config_change;


private:

  void update_user_defined_records(int id, RecordType type);

};                              /* End class LMRecords */

#endif /* _LM_RECORDS_H */
