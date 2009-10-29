About thread-plugin.c

This plugin sets up a callback to a continuation whose
handler function creates a thread.

INKPluginInit uses INKHttpHookAdd to create a 
continuation that is called back after the HTTP
origin server DNS lookup (the hook is 
INK_HTTP_OS_DNS_HOOK). The handler function
for the continuation is thread_plugin.

The thread_plugin function creates a thread using 
INKThreadCreate(), passing it the reenable_txn function
as follows:
INKThreadCreate(reenable_txn, edta);

The thread runs the reenable_txn function, which simply
reenables the HTTP transaction using INKHttpTxnReenable(). 
The thread is automatically destroyed when the function 
returns.
