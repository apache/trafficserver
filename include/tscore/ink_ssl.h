#if HAVE_BIO_SET_DATA == 0
#define HAVE_BIO_SET_DATA     1
#define BIO_set_data(a, _ptr) ((a)->ptr = (_ptr))
#endif

#if HAVE_BIO_GET_DATA == 0
#define HAVE_BIO_GET_DATA 1
#define BIO_get_data(a)   ((a)->ptr)
#endif

#if HAVE_BIO_GET_SHUTDOWN == 0
#define HAVE_BIO_GET_SHUTDOWN 1
#define BIO_get_shutdown(a)   ((a)->shutdown)
#endif

#if HAVE_BIO_METH_GET_CTRL == 0
#define HAVE_BIO_METH_GET_CTRL  1
#define BIO_meth_get_ctrl(biom) ((biom)->ctrl)
#endif

#if HAVE_BIO_METH_GET_CREATE == 0
#define HAVE_BIO_METH_GET_CREATE  1
#define BIO_meth_get_create(biom) ((biom)->create)
#endif

#if HAVE_BIO_METH_GET_DESTROY == 0
#define HAVE_BIO_METH_GET_DESTROY  1
#define BIO_meth_get_destroy(biom) ((biom)->destroy)
#endif
