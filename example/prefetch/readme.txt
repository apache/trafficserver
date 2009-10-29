The test-hns-plugin tests the Parent Traffic Server handling of parse/prefetch
rules. It prints information to 'stdout' at various stages to verify the
correctness of the parse/prefetch module. It has the following options:

-p. If 0, the plugin returns INK_PREFETCH_DISCONTINUE when called at the 
    INK_PREFETCH_PRE_PARSE_HOOK. If 1, the plugin returns
    INK_PREFETCH_CONTINUE.

-u. If 0, the plugin returns INK_PREFETCH_DISCONTINUE when called at the 
    INK_PREFETCH_EMBEDDED_URL_HOOK. If 1, the plugin returns
    INK_PREFETCH_CONTINUE.

-o. If 1, the plugin sets 'object_buf_status' field in the INKPrefetchInfo to
    INK_PREFETCH_OBJ_BUF_NEEDED and expects to be called back with the object.

-i. If 0, the plugin sets the 'url_response_proto' field in the 
    INKPrefetchInfo to INK_PREFETCH_PROTO_UDP. If 1, it sets the 
    'url_response_proto' field to INK_PREFETCH_PROTO_TCP.

-d. Specifies the directory where the plugin will store all the prefetched
    objects. All prefetched objects are stored in the PkgPreload format in 
    the 'prefetched.objects' file in this directory.



 
