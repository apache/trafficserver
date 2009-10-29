

User can run type-o-serve.pl as a client to establish a successful 
connection or any other client listen/accepting connections on a port. 

example entry for plugin.config: 
-------------------------------
INKAction.so npdev.inktomi.com 8080

No http request is needed to generate these results:


INK_EVENT_NET_CONNECT
---------------------

npdev> ./traffic_server -T"INKAction|http.*"   
[Nov  3 21:45:15.402] DIAG: (INKAction) gethostbyname( npdev.inktomi.com )
[Nov  3 21:45:15.404] DIAG: (INKAction) INKNetConnect(contp, client=(209.131.48.213/-779931435), port=(8080))
[Nov  3 21:45:15.407] DIAG: (INKAction) INKNetConnect: INK_EVENT_NET_CONNECT
[Nov  3 21:45:15.409] DIAG: (INKAction) INKNetConnect: plug-in has been called 
[Nov  3 21:45:45.674] DIAG: (INKAction) INKAction: INK_EVENT_TIMEOUT



INK_EVENT_NET_CONNECT_FAILED
---------------------------- 
npdev> ./traffic_server -T"INKAction|http.*"    
[Nov  3 21:49:33.828] DIAG: (INKAction) gethostbyname( npdev.inktomi.com )
[Nov  3 21:49:33.830] DIAG: (INKAction) INKNetConnect(contp, client=(209.131.48.213/-779931435), port=(8080))
[Nov  3 21:49:33.833] DIAG: (INKAction) INKNetConnect: INK_EVENT_NET_CONNECT_FAILED ***** 
[Nov  3 21:49:33.835] DIAG: (INKAction) INKNetConnect: plug-in has been called 
[Nov  3 21:50:05.537] DIAG: (INKAction) INKAction: INK_EVENT_TIMEOUT


Subsequent request can be made but will not involve plug-in interaction. 

