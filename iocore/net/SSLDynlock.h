extern struct CRYPTO_dynlock_value *ssl_dyn_create_callback(const char *file, int line);
extern void ssl_dyn_lock_callback(int mode, struct CRYPTO_dynlock_value *value, const char *file, int line);
extern void ssl_dyn_destroy_callback(struct CRYPTO_dynlock_value *value, const char *file, int line);
