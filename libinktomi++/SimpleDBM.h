/** @file

  Encapsulate a simple DBM

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

  @section details Details

  This C++ class encapsulates a simple DBM.  It is implemented
  internally with libdb or gdbm or a similar basic DBM.  The underlying
  implementation can be specified in the constructor, though at the time
  of this writing, only a libdb version is available.

  The SimpleDBM interface supports open, get, put, remove, iterate,
  and other methods.  It basically creates an associative object memory
  on disk.

  The data structure is thread safe, so the same SimpleDBM object can be
  used transparently across multiple threads.  If a thread wants several
  consecutive operations to be executed atomically, it must call the
  lock() methods to lock the database.  The lock() methods also can be
  used to prevent multiple processes from accessing the same database.

  Refer to the method comments in SimpleDBM.cc for more info.

*/

#ifndef _SimpleDBM_h_
#define	_SimpleDBM_h_

extern "C"
{
#ifdef HAVE_DB_185_H
#include <db_185.h>
#endif
#ifdef HAVE_DB_H
#include <db.h>
#endif
}

#include "ink_platform.h"
#include "inktomi++.h"


class SimpleDBM;

//////////////////////////////////////////////////////////////////////////////
//
//      Constants and Type Definitions
//
//////////////////////////////////////////////////////////////////////////////

typedef enum
{
  SimpleDBM_Type_LIBDB_Hash = 1,
  SimpleDBM_Type_GDBM = 2
} SimpleDBM_Type;


//////////////////////////////////////////////////////////////////////////////
//
//      SimpleDBM_Info
//
//////////////////////////////////////////////////////////////////////////////

typedef struct
{
  void *dbopen_info;
} SimpleDBM_Info_LIBDB;

typedef struct
{
  union
  {
    SimpleDBM_Info_LIBDB libdb;
  } type_specific;
} SimpleDBM_Info;


//////////////////////////////////////////////////////////////////////////////
//
//      SimpleDBMIteratorFunction
//
//////////////////////////////////////////////////////////////////////////////

typedef int (*SimpleDBMIteratorFunction)
  (SimpleDBM * dbm, void *client_data, void *key, int key_len, void *data, int data_len);


//////////////////////////////////////////////////////////////////////////////
//
//      SimpleDBM Class
//
//////////////////////////////////////////////////////////////////////////////

class SimpleDBM
{
private:
  int dbm_fd;
  char *dbm_name;
  bool dbm_opened;
  SimpleDBM_Type dbm_type;

  ProcessMutex thread_lock;

  union
  {
    struct
    {
      DB *db;
    } libdb;
  } type_specific_state;

public:
    SimpleDBM(SimpleDBM_Type type = SimpleDBM_Type_LIBDB_Hash);
   ~SimpleDBM();

  int open(char *db_name, SimpleDBM_Info * info = NULL);
  int close();

  int get(void *key, int key_len, void **data, int *data_len);
  int put(void *key, int key_len, void *data, int data_len);
  int remove(void *key, int key_len);
  int iterate(SimpleDBMIteratorFunction f, void *client_data);
  int sync();

  int lock(bool shared_lock = false);
  int unlock();

  void freeData(void *data);
};

//////////////////////////////////////////////////////////////////////////////
//
//      Inline Methods
//
//////////////////////////////////////////////////////////////////////////////

#endif /*_SimpleDBM_h_*/
