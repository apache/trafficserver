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
 * ProcessRecords.h
 *   Process Records Class, derived from BaseRecords. Class provides access
 * to configuration information to proxy processes and records/updates 
 * statistics information for sharing with the outside world.
 *
 * $Date: 2003-06-01 18:37:18 $
 *
 * 
 */

#ifndef _PROCESS_RECORDS_H
#define _PROCESS_RECORDS_H

#include "BaseRecords.h"


class ProcessRecords:public BaseRecords
{

public:

  ProcessRecords(char *mpath, char *cfile, char *lmfile):BaseRecords(mpath, cfile, lmfile)
  {
    ignore_manager = false;
  };
  ~ProcessRecords() {
  };

  /*
   * registerStatUpdateFunc(...)
   *   Function provides mechanism of registering update functions for stats, the
   * nature of the function is discussed above. Calling this function replaces any
   * preexisting callback function.
   *
   * Returns:    false              invalid stat id/name passed in
   *             true               success
   */
  inkcoreapi bool registerStatUpdateFunc(const char *name, RecordUpdateFunc func, void *odata = NULL);
  inkcoreapi bool registerStatUpdateFunc(int id, RecordUpdateFunc func, void *odata = NULL);

  bool registerConfigUpdateFunc(int id, RecordChangeFunc func, void *odata = NULL);
  inkcoreapi bool registerConfigUpdateFunc(const char *name, RecordChangeFunc func, void *odata = NULL);

  bool registerLocalUpdateFunc(int id, RecordChangeFunc func, void *odata = NULL);
  bool registerLocalUpdateFunc(const char *name, RecordChangeFunc func, void *odata = NULL);

  /*
   * readConfig*(...)
   *   Read functions provide read access to config information. 
   *
   * Returns:    data             impossible to gauge success from this, if
   *                              concerned pass in bool flag(true on success).
   */
  MgmtIntCounter readConfigCounter(int id, bool * found = NULL);
  MgmtInt readConfigInteger(int id, bool * found = NULL);
  MgmtFloat readConfigFloat(int id, bool * found = NULL);
  MgmtString readConfigString(int id, bool * found = NULL);

  MgmtIntCounter readConfigCounter(const char *name, bool * found = NULL);
  inkcoreapi MgmtInt readConfigInteger(const char *name, bool * found = NULL);
  MgmtFloat readConfigFloat(const char *name, bool * found = NULL);
  inkcoreapi MgmtString readConfigString(const char *name, bool * found = NULL);

  /*
   * readConfig*(...)
   *   Read functions provide read access to local node information. 
   *
   * Returns:    data             impossible to gauge success from this, if
   *                              concerned pass in bool flag(true on success).
   */
  MgmtIntCounter readLocalCounter(int id, bool * found = NULL);
  MgmtInt readLocalInteger(int id, bool * found = NULL);
  MgmtFloat readLocalFloat(int id, bool * found = NULL);
  MgmtString readLocalString(int id, bool * found = NULL);

  MgmtIntCounter readLocalCounter(const char *name, bool * found = NULL);
  MgmtInt readLocalInteger(const char *name, bool * found = NULL);
  MgmtFloat readLocalFloat(const char *name, bool * found = NULL);
  MgmtString readLocalString(const char *name, bool * found = NULL);

  void syncRecords();

  bool ignore_manager;

private:

};                              /* End class ProcessRecords */

#endif /* _PROCESS_RECORDS_H */
