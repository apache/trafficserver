This plugin sets up a callback to a continuation whose
handler function creates a thread.

TSPluginInit uses TSHttpHookAdd to create a
continuation that is called back after the HTTP
origin server DNS lookup (the hook is
TS_HTTP_OS_DNS_HOOK). The handler function
for the continuation is thread_plugin.

The thread_plugin function creates a thread using
TSThreadCreate(), passing it the reenable_txn function
as follows:
TSThreadCreate(reenable_txn, edta);

The thread runs the reenable_txn function, which simply
reenables the HTTP transaction using TSHttpTxnReenable().
The thread is automatically destroyed when the function
returns.
