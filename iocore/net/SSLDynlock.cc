/** @file

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

#include <openssl/crypto.h>

#include "ts/ink_platform.h"
#include "ts/ink_mutex.h"
#include "ts/ink_assert.h"
#include "ts/Diags.h"

struct CRYPTO_dynlock_value {
  CRYPTO_dynlock_value(const char *f, int l) : file(f), line(l) { ink_mutex_init(&mutex); }
  ~CRYPTO_dynlock_value() { ink_mutex_destroy(&mutex); }
  const char *file;
  int line;
  ink_mutex mutex;
};

struct CRYPTO_dynlock_value *
ssl_dyn_create_callback(const char *file, int line)
{
  Debug("v_ssl_lock", "file: %s line: %d", file, line);

  CRYPTO_dynlock_value *value = new CRYPTO_dynlock_value(file, line);
  return value;
}

// The locking callback goes away with openssl 1.1 and CRYPTO_LOCK is on longer defined
#ifdef CRYPTO_LOCK
void
ssl_dyn_lock_callback(int mode, struct CRYPTO_dynlock_value *value, const char *file, int line)
{
  Debug("v_ssl_lock", "file: %s line: %d", file, line);

  if (mode & CRYPTO_LOCK) {
    ink_mutex_acquire(&value->mutex);
  } else if (mode & CRYPTO_UNLOCK) {
    ink_mutex_release(&value->mutex);
  } else {
    Debug("ssl", "invalid SSL locking mode 0x%x", mode);
    ink_release_assert(0);
  }
}
#endif

void
ssl_dyn_destroy_callback(struct CRYPTO_dynlock_value *value, const char *file, int line)
{
  Debug("v_ssl_lock", "file: %s line: %d", file, line);
  delete value;
}
