The basic_auth.c plugin performs basic HTTP proxy authentication.

-- The plugin checks all client request headers for the Proxy-Authorization
   MIME field, which should contain the user name and password.

     TSPluginInit sets up a global HTTP hook that calls the plugin
     whenever there is a host DNS lookup. The plugin's continuation
     handler, auth-plugin, calls handle_dns to check the
     Proxy-Authorization field.

     handle_dns uses TSHttpTxnClientReqGet and TSMimeHdrFieldFind
     to obtain the Proxy-Authorization field.

-- If the request does not have the Proxy-Authorization field,
   the plugin sends the 407 Proxy authorization required status
   code back to the client. (The client should then prompt the
   user for a user name and password, and resend the request
   with the Proxy-Authorization field filled in.)

     If handle_dns does not find a Proxy-Authorization field,
     it adds a SEND_RESPONSE_HDR_HOOK to the transaction being
     processed; this means that Traffic Server will call the
     plugin back when sending the client response.

     handle_dns also reenables the transaction with
     TS_EVENT_HTTP_ERROR, which means that the plugin wants
     Traffic Server to terminate the transaction.

     When Traffic Server terminates the transaction, it
     sends the client an error message. Because of the
     SEND_RESPONSE_HDR_HOOK, Traffic Server calls the plugin
     back. The auth-plugin routine calls handle_response to
     send the client a 407 status code.

     When the client resends the request with the Proxy-
     Authorization field, a new transaction begins.

-- If the Proxy-Authorization MIME field is present, the plugin
   checks that the authentication scheme is "Basic".

     handle_dns uses TSMimeFieldValueStringGet to get the value
     of the Proxy-Authorization field.

-- The plugin then obtains the base64-encoded user name and password
   from the Proxy-Authorization MIME field.

     handle_dns calls base64_decode to decode the user name
     and password.

-- This plugin checks the validity of the user name and password.
   If the client is authenticated, the transaction proceeds. If
   the client is not authenticated, the plugin sends the client
   a 407 status code and terminates the transaction.

     handle_dns calls authorized to validate the user name and
     password. You can supply your own validation mechanism.
