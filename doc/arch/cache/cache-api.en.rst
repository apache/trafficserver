.. include:: common.defs

===========================
Cache Related API functions
===========================

.. c:function:: void TSHttpTxnReqCacheableSet(TSHttpTxn txnp, int flag)

   Set a *flag* that marks a request as cacheable. This is a positive override only, setting *flag* to 0 restores the default behavior, it does not force the request to be uncacheable.

.. c:function:: TSReturnCode TSCacheUrlSet(TSHttpTxn txnp, char const* url, int length)

  Set the cache key for the transaction *txnp* as the string pointed at by *url* of *length* characters. It need not be ``null`` terminated. This should be called from ``TS_HTTP_READ_REQUEST_HDR_HOOK`` which is before cache lookup but late enough that the HTTP request header is available.

===============
Cache Internals
===============

.. cpp:function::    int DIR_SIZE_WITH_BLOCK(int big)

    A preprocessor macro which computes the maximum size of a fragment based on the value of *big*. This is computed as if the argument where the value of the *big* field in a struct :cpp:class:`Dir`.

.. cpp:function::    int DIR_BLOCK_SIZE(int big)

    A preprocessor macro which computes the block size multiplier for a struct :cpp:class:`Dir` where *big* is the *big* field value.
