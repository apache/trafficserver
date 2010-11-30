

User can run type-o-serve.pl as a client to establish a successful 
connection or any other client listen/accepting connections on a port. 

example entry for plugin.config: 
-------------------------------
TSAction.so npdev.inktomi.com 8080

No http request is needed to generate these results:


TS_EVENT_NET_CONNECT
---------------------

npdev> ./traffic_server -T"TSAction|http.*"   
[Nov  3 21:45:15.402] DIAG: (TSAction) gethostbyname( npdev.inktomi.com )
[Nov  3 21:45:15.404] DIAG: (TSAction) TSNetConnect(contp, client=(209.131.48.213/-779931435), port=(8080))
[Nov  3 21:45:15.407] DIAG: (TSAction) TSNetConnect: TS_EVENT_NET_CONNECT
[Nov  3 21:45:15.409] DIAG: (TSAction) TSNetConnect: plug-in has been called 
[Nov  3 21:45:45.674] DIAG: (TSAction) TSAction: TS_EVENT_TIMEOUT



TS_EVENT_NET_CONNECT_FAILED
---------------------------- 
npdev> ./traffic_server -T"TSAction|http.*"    
[Nov  3 21:49:33.828] DIAG: (TSAction) gethostbyname( npdev.inktomi.com )
[Nov  3 21:49:33.830] DIAG: (TSAction) TSNetConnect(contp, client=(209.131.48.213/-779931435), port=(8080))
[Nov  3 21:49:33.833] DIAG: (TSAction) TSNetConnect: TS_EVENT_NET_CONNECT_FAILED ***** 
[Nov  3 21:49:33.835] DIAG: (TSAction) TSNetConnect: plug-in has been called 
[Nov  3 21:50:05.537] DIAG: (TSAction) TSAction: TS_EVENT_TIMEOUT


Subsequent request can be made but will not involve plug-in interaction. 

