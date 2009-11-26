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
  
  This C++ class encapsulates a simple DBM.  It is implemented internally
  with libdb or gdbm or a similar basic DBM.  The underlying implementation
  can be specified in the constructor, though at the time of this writing,
  only a libdb version is available.

  The SimpleDBM interface supports open, get, put, delete, iterate, and
  other methods.  It basically creates an associative object memory on disk.

  The data structure is thread safe, so the same SimpleDBM object can be used
  transparently across multiple threads.  If a thread wants several
  consecutive operations to be executed atomically, it must call the lock()
  methods to lock the database.  The lock() methods also can be used to
  prevent multiple processes from accessing the same database.

*/

#include "inktomi++.h"
#include "SimpleDBM.h"
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

/**
  This is the constructor for a SimpleDBM. The constructor initializes
  a DBM handle for a SimpleDBM of type &lt;type>, but does not create or
  attach to any database. The open() call is used for that purpose.

  If you use an unsupported type, no error is provided, but all subsequent
  operations will return error ENOTSUP.

*/
SimpleDBM::SimpleDBM(SimpleDBM_Type type)
{
  dbm_opened = false;
  dbm_type = type;
  dbm_fd = -1;
  dbm_name = NULL;

  ink_ProcessMutex_init(&thread_lock, "SimpleDBM");
}

/**
  This is the destructor for a SimpleDBM. If the attached database is
  already opened, this routine will close it before destruction.

*/
SimpleDBM::~SimpleDBM()
{
  if (dbm_opened)
    close();
  ink_ProcessMutex_destroy(&thread_lock);
}

/**
  This routine opens a database file with name db_name, creating it if
  it doesn't exist. You can specify optional, type-specific open info
  via info.

  If you perform any of the following methods on a SimpleDBM which has
  not been opened, the error ENOTCONN is returned.

  @return 0 on success, else a negative number on error.

*/
int
SimpleDBM::open(char *db_name, SimpleDBM_Info * info)
{
  DB *db;
  int return_code;

  ink_ProcessMutex_acquire(&thread_lock);

  if (dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-EALREADY);
  }

  if (db_name == NULL) {
    ink_ProcessMutex_release(&thread_lock);
    return (-EINVAL);
  }

  dbm_name = xstrdup(db_name);

  switch (dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
    db = dbopen(db_name, O_RDWR | O_CREAT, 0666, DB_HASH, (info ? info->type_specific.libdb.dbopen_info : NULL));

    type_specific_state.libdb.db = db;

    if (!db) {
      return_code = (errno ? -errno : -1);
    } else {
      return_code = 0;
      dbm_fd = db->fd(db);
      dbm_opened = true;
    }
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  ink_ProcessMutex_release(&thread_lock);
  return (return_code);
}


/**
  Sync and close the attachment to the current database.

  @return 0 on success, a negative number on error.

*/
int
SimpleDBM::close()
{
  int s, return_code;

  return_code = 0;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  switch (dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
    s = type_specific_state.libdb.db->close(type_specific_state.libdb.db);
    if (s != 0)
      return_code = -errno;
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  xfree(dbm_name);
  dbm_name = NULL;
  dbm_fd = -1;
  dbm_opened = false;

  ink_ProcessMutex_release(&thread_lock);
  return (return_code);
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
  DB *db;
  int s, return_code;
  DBT key_thang, data_thang;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  switch (dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
    db = type_specific_state.libdb.db;
    key_thang.data = key;
    key_thang.size = key_len;

    s = db->get(db, &key_thang, &data_thang, 0);

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
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  ink_ProcessMutex_release(&thread_lock);
  return (return_code);
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
  DB *db;
  int s, return_code;
  DBT key_thang, data_thang;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  switch (dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
    db = type_specific_state.libdb.db;
    key_thang.data = key;
    key_thang.size = key_len;
    data_thang.data = data;
    data_thang.size = data_len;

    s = db->put(db, &key_thang, &data_thang, 0);

    if (s == 0)
      return_code = 0;
    else if (s == 1)
      return_code = -EEXIST;
    else
      return_code = (errno ? -errno : -1);

    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  ink_ProcessMutex_release(&thread_lock);
  return (return_code);
}

/**
  This method removes any binding for the key with byte count key_len.

  @return 0 on success, a negative error number on failure.

*/
int
SimpleDBM::remove(void *key, int key_len)
{
  DB *db;
  DBT key_thang;
  int s, return_code;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  switch (dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
    db = type_specific_state.libdb.db;
    key_thang.data = key;
    key_thang.size = key_len;

    s = db->del(db, &key_thang, 0);

    if ((s == 0) || (s == 1))
      return_code = 0;
    else
      return_code = (errno ? -errno : -1);
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  ink_ProcessMutex_release(&thread_lock);
  return (return_code);
}

/**
  This method frees the data pointer that was returned by the
  SimpleDBM::get() method. If the user doesn't free the data returned
  by get() when it is no longer needed, it will become a storage leak.

*/
void
SimpleDBM::freeData(void *data)
{
  xfree(data);
}

/**
  Flush any information, if appropriate.

  @return 0 on success, a negative error number on failure.

*/
int
SimpleDBM::sync()
{
  DB *db;
  int s, return_code;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  switch (dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
    db = type_specific_state.libdb.db;

    s = db->sync(db, 0);

    if (s == 0)
      return_code = 0;
    else
      return_code = (errno ? -errno : -1);
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  ink_ProcessMutex_release(&thread_lock);
  return (return_code);
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
  DB *db;
  DBT key_thang, data_thang;
  int s, r, nelems, flags, return_code;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  switch (dbm_type) {
  case SimpleDBM_Type_LIBDB_Hash:
    db = type_specific_state.libdb.db;

    nelems = 0;
    return_code = 0;

    while (1) {
      flags = (nelems == 0 ? R_FIRST : R_NEXT);

      s = db->seq(db, &key_thang, &data_thang, flags);

      if (s == 1)               // all done
      {
        r = (*f) (this, client_data, NULL, 0, NULL, 0);
        return_code = nelems;
        break;
      } else if (s < 0)         // error
      {
        return_code = s;
        break;
      } else                    // got real data
      {
        r = (*f) (this, client_data, key_thang.data, (int) (key_thang.size), data_thang.data, (int) (data_thang.size));
        ++nelems;
        if (r == 0) {
          return_code = nelems;
          break;
        }
      }
    }
    break;
  default:
    return_code = -ENOTSUP;
    break;
  }

  ink_ProcessMutex_release(&thread_lock);
  return (return_code);
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

  @return 0 on success, a negative value on error.

*/
int
SimpleDBM::lock(bool shared_lock)
{
  int s;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  if (dbm_fd == -1) {
    ink_ProcessMutex_release(&thread_lock);
    return (-EBADF);
  }
  //
  // ink_file_lock can block, so we shouldn't leave the mutex acquired,
  // or we might never be able to unlock the file lock.
  //

  ink_ProcessMutex_release(&thread_lock);
  s = ink_file_lock(dbm_fd, (shared_lock ? F_RDLCK : F_WRLCK));

  return (s < 0 ? s : 0);
}

/**
  This routine releases a process lock on the database.

  The process locks are implemented with fcntl. Read Stevens'
  "Advanced Programming in the UNIX Environment", p.373 for some of
  the implications. In particular, locks do not increment. If a process
  opens a database D and locks it, and then another code path (or thread)
  opens the same file D, and locks and unlocks the lock, the original
  lock will be unset.

  @return 0 on success, a negative value on error.

*/
int
SimpleDBM::unlock()
{
  int s;

  ink_ProcessMutex_acquire(&thread_lock);

  if (!dbm_opened) {
    ink_ProcessMutex_release(&thread_lock);
    return (-ENOTCONN);
  }

  if (dbm_fd == -1) {
    ink_ProcessMutex_release(&thread_lock);
    return (-EBADF);
  }

  ink_ProcessMutex_release(&thread_lock);
  s = ink_file_lock(dbm_fd, F_UNLCK);

  return (s < 0 ? s : 0);
}
