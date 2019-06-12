async-test.c is source for a sample openssl crypto engine.  It wraps the standard RSA operations.
For the private key operations it spawns a thread to sleep for 5 seconds and then pauses the asynchronous job.

It should be built as follows.  It must be build against openssl 1.1 or better for access to the ASYNC_*_job apis.

gcc -fPIC -shared -g -o async-test.so -I<path to openssl headers> -L<path to openssl library> -lssl -lcrypto -lpthread async_engine.c

load_engine.cnf is an example openssl config file that can be passed to Traffic Server via the proxy.config.ssl.engine_cnf_file setting.
It describes which crypto engines should be loaded and how they should be used.  In the case of our async-test crypto engine it will be used for
RSA operations
