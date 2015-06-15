#include "libts.h"

struct CRYPTO_dynlock_value
{
  CRYPTO_dynlock_value(const char *f, int l): file(f), line(l) {
    pthread_mutex_init(&mutex, NULL);
  }
  ~CRYPTO_dynlock_value() {
    pthread_mutex_destroy(&mutex);
  }
  const char *file;
  int line;
  pthread_mutex_t mutex;
};

struct CRYPTO_dynlock_value *ssl_dyn_create_callback(const char *file, int line)
{
  Debug("v_ssl_lock", "file: %s line: %d", file, line);

  CRYPTO_dynlock_value *value = new CRYPTO_dynlock_value(file, line);
  return value;
}

void ssl_dyn_lock_callback(int mode, struct CRYPTO_dynlock_value *value, const char *file, int line)
{
  Debug("v_ssl_lock", "file: %s line: %d", file, line);

  if (mode & CRYPTO_LOCK) {
    pthread_mutex_lock(&value->mutex);
  } else if (mode & CRYPTO_UNLOCK) {
    pthread_mutex_unlock(&value->mutex);
  } else {
    Debug("ssl", "invalid SSL locking mode 0x%x", mode);
    ink_release_assert(0);
  }
}

void ssl_dyn_destroy_callback(struct CRYPTO_dynlock_value *value, const char *file, int line)
{
  Debug("v_ssl_lock", "file: %s line: %d", file, line);

  delete value;
}
