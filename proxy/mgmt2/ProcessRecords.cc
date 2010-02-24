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
 * ProcessRecords.cc
 *   Member function definitions for the Process Records class.
 *
 * $Date: 2003-06-01 18:37:18 $
 *
 * 
 */

#include "ink_config.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */
#include "ProcessRecords.h"

bool
ProcessRecords::registerStatUpdateFunc(const char *name, RecordUpdateFunc func, void *odata)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type) && (type == PROCESS || type == PLUGIN)) {
    return (registerUpdateFunc(id, type, func, odata));
  }
  return false;
}                               /* End ProcessRecords::registerStatUpdateFunct */


bool
ProcessRecords::registerConfigUpdateFunc(int id, RecordChangeFunc func, void *odata)
{
  return (registerChangeFunc(id, CONFIG, func, odata));
}                               /* End ProcessRecords::registerConfigUpdateFunc */


bool
ProcessRecords::registerConfigUpdateFunc(const char *name, RecordChangeFunc func, void *odata)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type) && type == CONFIG) {
    return (registerConfigUpdateFunc(id, func, odata));
  }
  return false;
}                               /* Rnf ProcessRecords::registerConfigUpdateFunc */


bool
ProcessRecords::registerLocalUpdateFunc(int id, RecordChangeFunc func, void *odata)
{
  return (registerChangeFunc(id, LOCAL, func, odata));
}                               /* End ProcessRecords::registerConfigUpdateFunc */


bool
ProcessRecords::registerLocalUpdateFunc(const char *name, RecordChangeFunc func, void *odata)
{
  int id;
  RecordType type;

  if (idofRecord(name, &id, &type) && type == LOCAL) {
    return (registerLocalUpdateFunc(id, func, odata));
  }
  return false;
}                               /* Rnf ProcessRecords::registerLocalUpdateFunc */


MgmtIntCounter
ProcessRecords::readConfigCounter(int id, bool * found)
{
  if (found)
    *found = false;
  return (readCounter(id, CONFIG, found));
}                               /* End ProcessRecords::readConfigCounter */


MgmtInt
ProcessRecords::readConfigInteger(int id, bool * found)
{
  if (found)
    *found = false;
  return (readInteger(id, CONFIG, found));
}                               /* End ProcessRecords::readConfigInteger */


MgmtFloat
ProcessRecords::readConfigFloat(int id, bool * found)
{
  if (found)
    *found = false;
  return (readFloat(id, CONFIG, found));
}                               /* End ProcessRecords::readConfigFloat */


MgmtString
ProcessRecords::readConfigString(int id, bool * found)
{
  if (found)
    *found = false;
  return (readString(id, CONFIG, found));
}                               /* End ProcessRecords::readConfigString */


MgmtIntCounter
ProcessRecords::readConfigCounter(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == CONFIG) {
    return (readCounter(id, CONFIG, found));
  }
  return INVALID;
}                               /* End ProcessRecords::readConfigInteger */


MgmtInt
ProcessRecords::readConfigInteger(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == CONFIG) {
    return (readInteger(id, CONFIG, found));
  }
  return INVALID;
}                               /* End ProcessRecords::readConfigInteger */


MgmtFloat
ProcessRecords::readConfigFloat(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == CONFIG) {
    return (readFloat(id, CONFIG, found));
  }
  return INVALID;

}                               /* End ProcessRecords::readConfigFloat */


MgmtString
ProcessRecords::readConfigString(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == CONFIG) {
    return (readString(id, CONFIG, found));
  }
  return NULL;
}                               /* End ProcessRecords::readConfigString */

MgmtIntCounter
ProcessRecords::readLocalCounter(int id, bool * found)
{
  if (found)
    *found = false;
  return (readCounter(id, LOCAL, found));
}                               /* End ProcessRecords::readLocalCounter */


MgmtInt
ProcessRecords::readLocalInteger(int id, bool * found)
{
  if (found)
    *found = false;
  return (readInteger(id, LOCAL, found));
}                               /* End ProcessRecords::readLocalInteger */


MgmtFloat
ProcessRecords::readLocalFloat(int id, bool * found)
{
  if (found)
    *found = false;
  return (readFloat(id, LOCAL, found));
}                               /* End ProcessRecords::readLocalFloat */


MgmtString
ProcessRecords::readLocalString(int id, bool * found)
{
  if (found)
    *found = false;
  return (readString(id, LOCAL, found));
}                               /* End ProcessRecords::readLocalString */


MgmtIntCounter
ProcessRecords::readLocalCounter(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == LOCAL) {
    return (readCounter(id, LOCAL, found));
  }
  return INVALID;
}                               /* End ProcessRecords::readLocalInteger */


MgmtInt
ProcessRecords::readLocalInteger(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == LOCAL) {
    return (readInteger(id, LOCAL, found));
  }
  return INVALID;
}                               /* End ProcessRecords::readLocalInteger */


MgmtFloat
ProcessRecords::readLocalFloat(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == LOCAL) {
    return (readFloat(id, LOCAL, found));
  }
  return INVALID;

}                               /* End ProcessRecords::readLocalFloat */


MgmtString
ProcessRecords::readLocalString(const char *name, bool * found)
{
  int id;
  RecordType type;

  if (found)
    *found = false;
  if (idofRecord(name, &id, &type) && type == LOCAL) {
    return (readString(id, LOCAL, found));
  }
  return NULL;
}                               /* End ProcessRecords::readLocalString */

void
ProcessRecords::syncRecords()
{
  syncPutRecords(PROCESS, str_pid);
  syncPutRecords(PLUGIN, str_pid);
  if (!ignore_manager) {
    syncGetRecords(LOCAL, NULL);
    syncGetRecords(CONFIG, NULL);
//      INKqa12757: Disabling the manager changes I did to fix INKqa06971
//      syncGetRecords(NODE, NULL);
//      syncGetRecords(CLUSTER, NULL);
  }
  return;
}                               /* End ProcessRecords::syncPutRecords */
