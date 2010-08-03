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
  implementation can be specified in the constructor, currently sqlite3
  and libdb are supported. sqlite3 is the favored default if available.

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
    SimpleDBM_Type_SQLITE3 = 2,
    SimpleDBM_Type_MDBM = 4 // Not supported!
  } SimpleDBM_Type;

typedef enum
  {
    SimpleDBM_Flags_READONLY = 1
  } SimpleDBM_Flags;

extern "C"
{
#if ATS_USE_SQLITE3
# define DEFAULT_DB_IMPLEMENTATION SimpleDBM_Type_SQLITE3
# define SIMPLEDBM_USE_SQLITE3
# ifdef HAVE_SQLITE3_H
#  include <sqlite3.h>
# else
#  error Cannot use sqlite3 without sqlite3.h header
# endif
#endif

#if ATS_USE_LIBDB
# define DEFAULT_DB_IMPLEMENTATION SimpleDBM_Type_LIBDB_Hash
# define SIMPLEDBM_USE_LIBDB
# ifdef HAVE_DB_185_H
#  include <db_185.h>
# else
#  ifdef HAVE_DB_H
#   include <db.h>
#  else
#    error Undefined db header
#  endif
# endif
#endif
}


//////////////////////////////////////////////////////////////////////////////
//
//      SimpleDBMIteratorFunction
//
//////////////////////////////////////////////////////////////////////////////

typedef int (*SimpleDBMIteratorFunction)
  (SimpleDBM *dbm, void *client_data, const void *key, int key_len, const void *data, int data_len);


//////////////////////////////////////////////////////////////////////////////
//
//      SimpleDBM Class
//
//////////////////////////////////////////////////////////////////////////////
class SimpleDBM
{
private:
  int _dbm_fd;
  char *_dbm_name;
  bool _dbm_opened;
  SimpleDBM_Type _dbm_type;
  void *_data;

  ProcessMutex _lock;

  // Backend type specific "state" data.
  union {
#ifdef SIMPLEDBM_USE_LIBDB
    struct {
      DB *db;
    } libdb;
#endif
#ifdef SIMPLEDBM_USE_SQLITE3
    struct {
      sqlite3 *ppDb;
      sqlite3_stmt *replace_stmt;
      sqlite3_stmt *delete_stmt;
      sqlite3_stmt *select_stmt;
      sqlite3_stmt *iterate_stmt;
    } sqlite3;
#endif
  } _type_state;

public:
  SimpleDBM(SimpleDBM_Type type = DEFAULT_DB_IMPLEMENTATION);

  ~SimpleDBM();

  int open(char *db_name, int flags = 0, void *info = NULL);
  int close();

  int get(void *key, int key_len, void **data, int *data_len);
  int put(void *key, int key_len, void *data, int data_len);
  int remove(void *key, int key_len);
  int iterate(SimpleDBMIteratorFunction f, void *client_data);
  int sync();

  int lock(bool shared_lock = false);
  int unlock();


  //////////////////////////////////////////////////////////////////////////////
  //
  //      Inline Methods
  //
  //////////////////////////////////////////////////////////////////////////////
  bool functional() const {
    return supported(_dbm_type);
  }

  /**
     This method frees the data pointer that was returned by the
     SimpleDBM::get() method. If the user doesn't free the data returned
     by get() when it is no longer needed, it will become a storage leak.

  */
  void freeData(void *data) {
    xfree(data);
  }

  // Class members, used to see what backends are available.
  static bool supported(SimpleDBM_Type type) {
    return static_cast<bool>(backends() & type);
  }

  static int backends() {
    return
#ifdef SIMPLEDBM_USE_LIBDB
      SimpleDBM_Type_LIBDB_Hash |
#endif
#ifdef SIMPLEDBM_USE_SQLITE3
      SimpleDBM_Type_SQLITE3 |
#endif
#ifdef SIMPLEDBM_USE_MDBM
      SimpleDBM_Type_MDBM |
#endif
      0;
  }

};

#endif /*_SimpleDBM_h_*/
