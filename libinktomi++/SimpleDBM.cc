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

  The SimpleDBM interface supports open, get, put, delete, iterate, and
  other methods.  It basically creates an associative object memory on disk.

  The data structure is thread safe, so the same SimpleDBM object can be used
  transparently across multiple threads.  If a thread wants several
  consecutive operations to be executed atomically, it must call the lock()
  methods to lock the database.  The lock() methods also can be used to
  prevent multiple processes from accessing the same database.

*/
#include "ink_config.h"

#include "SimpleDBM.h"

// sqlite3 prepared statement SQL. TODO: We might not be able to
// use these "globals" if we switch to a model where sqlite3 is allowed
// to do the locking around the db conn and step.
#ifdef SIMPLEDBM_USE_SQLITE3
#include "INK_MD5.h"

static const char* REPLACE_STMT = "REPLACE INTO ats(kid,key,val) VALUES(?,?,?)";
static const char* DELETE_STMT = "DELETE FROM ats WHERE kid=?";
static const char* SELECT_STMT = "SELECT val FROM ats WHERE kid=?";
static const char* ITERATE_STMT = "SELECT key,val FROM ats";

static const int SQLITE_RETRIES = 3;
static const int MD5_LENGTH = 32;
#endif


/**
  This is the constructor for a SimpleDBM. The constructor initializes
  a DBM handle for a SimpleDBM of type &lt;type>, but does not create or
  attach to any database. The open() call is used for that purpose.

  If you use an unsupported type, no error is provided, but all subsequent
  operations will return error ENOTSUP.

*/
SimpleDBM::SimpleDBM(SimpleDBM_Type type)
{
  _dbm_fd = -1;
  _dbm_name = NULL;
  _dbm_opened = false;
  _dbm_type = type;
  _data = NULL;

#ifdef SIMPLEDBM_USE_SQLITE3
  memset(&_type_state.sqlite3, 0, sizeof(_type_state.sqlite3));
#endif
  ink_ProcessMutex_init(&_lock, "SimpleDBM");
}


/**
  This is the destructor for a SimpleDBM. If the attached database is
  already opened, this routine will close it before destruction.

*/
SimpleDBM::~SimpleDBM()
{
  if (_dbm_opened)
    close();
  ink_ProcessMutex_destroy(&_lock);
}


/**
  This routine opens a database file with name db_name, creating it if
  it doesn't exist. You can specify optional, type-specific open info
  via info.

  If you perform any of the following methods on a SimpleDBM which has
  not been opened, the error ENOTCONN is returned.

  Flags can be used to control some behavior, which might or might not
  be supported by all backends:

      SimpleDBM_Flags_READONLY   - Open the DB in Read-Only mode

  @return 0 on success, else a negative number on error.

*/
int
SimpleDBM::open(char *db_name, int flags, void* info)
{
  int return_code = 0;
#ifdef SIMPLEDBM_USE_SQLITE3
  int sqlite3_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  int s;
#endif

  ink_ProcessMutex_acquire(&_lock);

  if (_dbm_opened) {
    ink_ProcessMutex_release(&_lock);
    return (-EALREADY);
  }

  if (db_name == NULL) {
    ink_ProcessMutex_release(&_lock);
    return (-EINVAL);
  }

  _dbm_name = xstrdup(db_name);

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    _type_state.libdb.db = dbopen(db_name, O_RDWR | O_CREAT, 0666, DB_HASH, info);

    if (!_type_state.libdb.db) {
      return_code = (errno ? -errno : -1);
    } else {
      _dbm_fd = _type_state.libdb.db->fd(_type_state.libdb.db);
      _dbm_opened = true;
    }
#else // libdb not supported
    return_code = -ENOTSUP;
#endif
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
#ifdef SQLITE_OPEN_NOMUTEX
    sqlite3_flags |= SQLITE_OPEN_NOMUTEX; // We use our own mutexes
#endif
    if (flags & SimpleDBM_Flags_READONLY)
      sqlite3_flags = SQLITE_OPEN_READONLY;
    s = sqlite3_open_v2(db_name, &_type_state.sqlite3.ppDb, sqlite3_flags, NULL);
    if (SQLITE_OK == s) {
      if (!(flags & SimpleDBM_Flags_READONLY))
        s = sqlite3_exec(_type_state.sqlite3.ppDb,
                         "CREATE TABLE IF NOT EXISTS ats(kid VARHCHAR(32) PRIMARY KEY, key BLOB, val BLOB)",
                         NULL, 0, NULL);
      if (SQLITE_OK == s) { // OK, DB opened and initialized properly, lets prepare the statements.
        _dbm_opened = true;
        if (!(flags & SimpleDBM_Flags_READONLY)) {
          s = sqlite3_prepare_v2(_type_state.sqlite3.ppDb, REPLACE_STMT, -1, &_type_state.sqlite3.replace_stmt, NULL);
          if (SQLITE_OK != s)
            return_code = -s;
          s = sqlite3_prepare_v2(_type_state.sqlite3.ppDb, DELETE_STMT, -1, &_type_state.sqlite3.delete_stmt, NULL);
          if (SQLITE_OK != s)
            return_code = -s;
        }
        s = sqlite3_prepare_v2(_type_state.sqlite3.ppDb, SELECT_STMT, -1, &_type_state.sqlite3.select_stmt, NULL);
        if (SQLITE_OK != s)
          return_code = -s;
        s = sqlite3_prepare_v2(_type_state.sqlite3.ppDb, ITERATE_STMT, -1, &_type_state.sqlite3.iterate_stmt, NULL);
        if (SQLITE_OK != s)
          return_code = -s;
      } else {
        return_code = -s;
      }
    } else {
      return_code = -s;
    }
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }
  ink_ProcessMutex_release(&_lock);

  return return_code;
}


/**
  Sync and close the attachment to the current database.

  @return 0 on success, a negative number on error.

*/
int
SimpleDBM::close()
{
  int return_code = 0;
  int s;

  ink_ProcessMutex_acquire(&_lock);

  xfree(_dbm_name);
  _dbm_name = NULL;

  if (!_dbm_opened) {
    ink_ProcessMutex_release(&_lock);
    return (-ENOTCONN);
  }

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    s = _type_state.libdb.db->close(_type_state.libdb.db);
    if (s != 0)
      return_code = -errno;
#else
    return_code = -ENOTSUP;
#endif // libdb not supported
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    sqlite3_finalize(_type_state.sqlite3.replace_stmt);
    sqlite3_finalize(_type_state.sqlite3.delete_stmt);
    sqlite3_finalize(_type_state.sqlite3.select_stmt);
    sqlite3_finalize(_type_state.sqlite3.iterate_stmt);
    s = sqlite3_close(_type_state.sqlite3.ppDb);
    if (SQLITE_OK != s)
      return_code = -s;
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  _dbm_fd = -1;
  _dbm_opened = false;
  ink_ProcessMutex_release(&_lock);

  return return_code;
}


/**
  This method finds the data object corresponding to a key, if any.

  If a corresponding data object is found, 0 is returned, and a pointer
  to a dynamically-allocated object data is returned via data.

  Note: the data pointer IS to dynamically allocated storage that will
  survive past this call. The user MUST free the data when it is done,
  using the SimpleDBM::freeData(void *) method.

  @return if data is NOT found, 1 is returned. If a system error occurs,
    a negative errno is returned. If data is found, 0 is returned.

*/
int
SimpleDBM::get(void *key, int key_len, void **data, int *data_len)
{
  int return_code = 0;
  int s;
#ifdef SIMPLEDBM_USE_LIBDB
  DBT key_thang, data_thang;
#endif
#ifdef SIMPLEDBM_USE_SQLITE3
  int retries = SQLITE_RETRIES;
  const void *res = NULL;
  INK_MD5 key_md5;
  char key_buf[33];

  // Do this before we acquire the lock
  key_md5.encodeBuffer((char*)key, key_len);
  key_md5.toHexStr(key_buf);
#endif

  ink_ProcessMutex_acquire(&_lock);

  if (!_dbm_opened) {
    ink_ProcessMutex_release(&_lock);
    return (-ENOTCONN);
  }

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    key_thang.data = key;
    key_thang.size = key_len;

    s = _type_state.libdb.db->get(_type_state.libdb.db, &key_thang, &data_thang, 0);

    if (s == 0) {
      *data_len = (int) (data_thang.size);
      *data = xmalloc((unsigned int) (data_thang.size));
      ink_memcpy(*data, data_thang.data, (int) (data_thang.size));
      return_code = 0;
    } else if (s == 1) {
      *data_len = 0;
      *data = NULL;
      return_code = 1;
    } else {
      *data_len = 0;
      *data = NULL;
      return_code = (errno ? -errno : -1);
    }
#else // libdb not supported
    return_code = -ENOTSUP;
#endif
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    sqlite3_reset(_type_state.sqlite3.select_stmt);
    sqlite3_bind_text(_type_state.sqlite3.select_stmt, 1, key_buf, MD5_LENGTH, NULL);
    while (retries > 0) {
      s = sqlite3_step(_type_state.sqlite3.select_stmt);
      switch (s) {
      case SQLITE_ROW:
        retries = 0;
        res = sqlite3_column_blob(_type_state.sqlite3.select_stmt, 0);
        if (res) {
          *data_len = sqlite3_column_bytes(_type_state.sqlite3.select_stmt, 0);
          *data = xmalloc((unsigned int)*data_len);
          ink_memcpy(*data, (void*)res, (int)*data_len);
        } else {
          *data_len = 0;
          *data = NULL;
        }
        return_code = 0;
        break;
      case SQLITE_BUSY:
        --retries;
        return_code = -s;
        break;
      case SQLITE_DONE:
        if (!res)
          return_code = 1;
        retries = 0;
        break;
      default:
        return_code = -s;
        retries = 0;
        break;
      }
    }
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }
  ink_ProcessMutex_release(&_lock);

  return return_code;
}


/**
  This method inserts the (key,data) binding into the current
  database. key_len provides the length of the key in bytes, and data_len
  provides the length of the data in bytes.

  @return 0 on success, a negative error number on failure.

*/
int
SimpleDBM::put(void *key, int key_len, void *data, int data_len)
{
  int return_code = 0;
  int s;
#ifdef SIMPLEDBM_USE_LIBDB
  DBT key_thang, data_thang;
#endif
#ifdef SIMPLEDBM_USE_SQLITE3
  int retries = SQLITE_RETRIES;
  INK_MD5 key_md5;
  char key_buf[33];

  // Do this before we acquire the lock
  key_md5.encodeBuffer((char*)key, key_len);
  key_md5.toHexStr(key_buf);
#endif

  ink_ProcessMutex_acquire(&_lock);

  if (!_dbm_opened) {
    ink_ProcessMutex_release(&_lock);
    return -ENOTCONN;
  }

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    key_thang.data = key;
    key_thang.size = key_len;
    data_thang.data = data;
    data_thang.size = data_len;

    s = _type_state.libdb.db->put(_type_state.libdb.db, &key_thang, &data_thang, 0);

    if (s == 0)
      return_code = 0;
    else if (s == 1)
      return_code = -EEXIST;
    else
      return_code = (errno ? -errno : -1);

#else // libdb not supported
    return_code = -ENOTSUP;
#endif
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    if (NULL == _type_state.sqlite3.replace_stmt)
      return -ENOTSUP;
    sqlite3_reset(_type_state.sqlite3.replace_stmt);
    sqlite3_bind_text(_type_state.sqlite3.replace_stmt, 1, key_buf, MD5_LENGTH, NULL);
    sqlite3_bind_blob(_type_state.sqlite3.replace_stmt, 2, key, key_len, NULL);
    sqlite3_bind_blob(_type_state.sqlite3.replace_stmt, 3, data, data_len, NULL);

    while (retries > 0) {
      s = sqlite3_step(_type_state.sqlite3.replace_stmt);
      switch (s) {
      case SQLITE_BUSY:
        --retries;
        return_code = -s;
        break;
      case SQLITE_DONE:
        retries = 0;
        break;
      default:
        return_code = -s;
        retries = 0;
        break;
      }
    }
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }
  ink_ProcessMutex_release(&_lock);

  return return_code;
}


/**
  This method removes any binding for the key with byte count key_len.

  @return 0 on success, a negative error number on failure.

*/
int
SimpleDBM::remove(void *key, int key_len)
{
  int return_code = 0;
  int s;
#ifdef SIMPLEDBM_USE_LIBDB
  DBT key_thang;
#endif
#ifdef SIMPLEDBM_USE_SQLITE3
  int retries = SQLITE_RETRIES;
  INK_MD5 key_md5;
  char key_buf[33];

  // Do this before we acquire the lock
  key_md5.encodeBuffer((char*)key, key_len);
  key_md5.toHexStr(key_buf);
#endif

  ink_ProcessMutex_acquire(&_lock);

  if (!_dbm_opened) {
    ink_ProcessMutex_release(&_lock);
    return (-ENOTCONN);
  }

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    key_thang.data = key;
    key_thang.size = key_len;

    s = _type_state.libdb.db->del(_type_state.libdb.db, &key_thang, 0);

    if ((s == 0) || (s == 1))
      return_code = 0;
    else
      return_code = (errno ? -errno : -1);
#else // libdb not supported
    return_code = -ENOTSUP;
#endif
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    if (NULL == _type_state.sqlite3.delete_stmt)
      return -ENOTSUP;
    sqlite3_reset(_type_state.sqlite3.delete_stmt);
    sqlite3_bind_text(_type_state.sqlite3.delete_stmt, 1, key_buf, MD5_LENGTH, NULL);
    while (retries > 0) {
      s = sqlite3_step(_type_state.sqlite3.delete_stmt);
      switch (s) {
      case SQLITE_BUSY:
        --retries;
        return_code = -s;
        break;
      case SQLITE_DONE:
        retries = 0;
        break;
      default:
        return_code = -s;
        retries = 0;
        break;
      }
    }
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }
  ink_ProcessMutex_release(&_lock);

  return return_code;
}


/**
  Flush any information, if appropriate.

  @return 0 on success, a negative error number on failure.

*/
int
SimpleDBM::sync()
{
  int return_code = 0;
#ifdef SIMPLEDBM_USE_LIBDB
  int s;
#endif

  ink_ProcessMutex_acquire(&_lock);

  if (!_dbm_opened) {
    ink_ProcessMutex_release(&_lock);
    return (-ENOTCONN);
  }

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    s = _type_state.libdb.db->sync(_type_state.libdb.db, 0);

    if (s == 0)
      return_code = 0;
    else
      return_code = (errno ? -errno : -1);
#else
    return_code = -ENOTSUP;
#endif // libdb not supported
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    // Todo: Implement sync ?
    return_code = 0;
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }
  ink_ProcessMutex_release(&_lock);

  return return_code;
}


/**
  This routine maps a function f over the elements in the database,
  calling f once for each element in the database. The function f is
  invoked with the following arguments:

  @code
  int f(SimpleDBM *dbm, void *client_data, void *key, int key_len,
        void *data, int data_len)
  @endcode

  The function f should return 1 to continue iterating, or 0 to terminate
  iteration. The last element of the iteration is indicated with key
  and data set to NULL.

  In this routine, key and data are internally managed state. The caller
  MUST NOT deallocate this data. If the user wants to maintain the data
  upon return from f, the user must copy the data.

  The client data is passed through the callback function f, which can
  help remember some iteration state.

  The database is locked in exclusive mode through the entire iteration.

  @return number of elements iterated is returned on success, or a
    negative number on error.

*/
int
SimpleDBM::iterate(SimpleDBMIteratorFunction f, void *client_data)
{
  int return_code = 0;
  int nelems = 0;
  int s, r;
#ifdef SIMPLEDBM_USE_LIBDB
  DBT key_thang, data_thang;
  int flags;
#endif
#ifdef SIMPLEDBM_USE_SQLITE3
  int retries = SQLITE_RETRIES;
  const void *key = NULL;
  const void *data = NULL;
#endif

  ink_ProcessMutex_acquire(&_lock);

  if (!_dbm_opened) {
    ink_ProcessMutex_release(&_lock);
    return (-ENOTCONN);
  }

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    while (1) {
      flags = (nelems == 0 ? R_FIRST : R_NEXT);

      s = _type_state.libdb.db->seq(_type_state.libdb.db, &key_thang, &data_thang, flags);

      if (s == 1) {
        r = (*f)(this, client_data, NULL, 0, NULL, 0);
        return_code = nelems;
        break;
      } else if (s < 0) {         // error
        return_code = s;
        break;
      } else {                   // got real data
        r = (*f)(this, client_data, key_thang.data, (int)(key_thang.size), data_thang.data, (int)(data_thang.size));
        ++nelems;
        if (r == 0) {
          return_code = nelems;
          break;
        }
      }
    }
#else // libdb not supported
    return_code = -ENOTSUP;
#endif
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    sqlite3_reset(_type_state.sqlite3.iterate_stmt);
    while (retries > 0) {
      s = sqlite3_step(_type_state.sqlite3.iterate_stmt);
      switch (s) {
      case SQLITE_ROW:
        key = sqlite3_column_blob(_type_state.sqlite3.iterate_stmt, 0);
        data = sqlite3_column_blob(_type_state.sqlite3.iterate_stmt, 0);
        if (key) {
          int key_len = sqlite3_column_bytes(_type_state.sqlite3.iterate_stmt, 0);
          int data_len = sqlite3_column_bytes(_type_state.sqlite3.iterate_stmt, 1);

          ++nelems;
          r = (*f)(this, client_data, key, key_len, data, data_len);
          if (r == 0) {
            return_code = nelems;
            retries = 0; // We're done, or so the callback thinks
          }
        }
        break;
      case SQLITE_BUSY:
        --retries;
        return_code = -s;
        break;
      case SQLITE_DONE:
        r = (*f)(this, client_data, NULL, 0, NULL, 0);
        return_code = nelems;
        retries = 0;
        break;
      default:
        return_code = -s;
        retries = 0;
        break;
      }
    }
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }
  ink_ProcessMutex_release(&_lock);

  return return_code;
}


/**
  This routine takes out a process lock on the database.

  The lock can either be a shared lock or an exclusive lock, depending on
  the value of shared_lock. Multiple processes may have a shared lock on
  the database at the same time. Only one process can have an exclusive
  lock on the database at one time. Shared and exclusive locks will not
  be held on the database at any one time.

  The process locks are implemented with fcntl. Read Stevens'
  "Advanced Programming in the UNIX Environment", p.373 for some of
  the implications. In particular, locks do not increment. If a process
  opens a database D and locks it, and then another code path (or thread)
  opens the same file D, and locks and unlocks the lock, the original
  lock will be unset.

  Note that not all backend implementation requires an explicit lock, since
  they handle multiple access to the DB automatically. In such cases, lock()
  and unlock() becomes no-ops.

  @return 0 on success, a negative value on error.
*/
int
SimpleDBM::lock(bool shared_lock)
{
  int return_code = 0;

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    ink_ProcessMutex_acquire(&_lock);
    if (!_dbm_opened) {
      ink_ProcessMutex_release(&_lock);
      return (-ENOTCONN);
    }

    if (_dbm_fd == -1) {
      ink_ProcessMutex_release(&_lock);
      return (-EBADF);
    }
    ink_ProcessMutex_release(&_lock);
    //
    // ink_file_lock can block, so we shouldn't leave the mutex acquired,
    // or we might never be able to unlock the file lock.
    //
    return_code = ink_file_lock(_dbm_fd, (shared_lock ? F_RDLCK : F_WRLCK));
    if (return_code > 0)
      return_code = 0;
#else // libdb not supported
    return_code = -ENOTSUP;
#endif
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    // Explicit locking not needed / supported for sqlite3.
    return_code = 0;
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  return (return_code);
}


/**
  This routine releases a process lock on the database.

  The process locks are implemented with fcntl. Read Stevens'
  "Advanced Programming in the UNIX Environment", p.373 for some of
  the implications. In particular, locks do not increment. If a process
  opens a database D and locks it, and then another code path (or thread)
  opens the same file D, and locks and unlocks the lock, the original
  lock will be unset.

  Note that not all backend implementation requires an explicit lock, since
  they handle multiple access to the DB automatically. In such cases, lock()
  and unlock() becomes no-ops.

  @return 0 on success, a negative value on error.

*/
int
SimpleDBM::unlock()
{
  int return_code = 0;

  switch (_dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
#ifdef SIMPLEDBM_USE_LIBDB
    ink_ProcessMutex_acquire(&_lock);
    if (!_dbm_opened) {
      ink_ProcessMutex_release(&_lock);
      return (-ENOTCONN);
    }

    if (_dbm_fd == -1) {
      ink_ProcessMutex_release(&_lock);
      return (-EBADF);
    }
    ink_ProcessMutex_release(&_lock);

    return_code = ink_file_lock(_dbm_fd, F_UNLCK);
    if (return_code > 0)
      return_code = 0;
#else // libdb not supported
    return_code = -ENOTSUP;
#endif
    break;
  case SimpleDBM_Type_SQLITE3:
#ifdef SIMPLEDBM_USE_SQLITE3
    // Locking not needed / supported for sqlite3.
    return_code = 0;
#else // sqlite3 not supported
    return_code = -ENOTSUP;
#endif
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  return return_code;
}
