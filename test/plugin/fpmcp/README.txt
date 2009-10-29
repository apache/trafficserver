
INKHttpTxnCacheLookupStatusGet
--------------------------------------
Approach 

Gather expected test data sent as an 
extension header in the client 
request. Store this data localy.

The one unique test is for HIT_STALE,
where the plug-in code test for 
an expected HIT_STALE and if this 
is found, the document being requested 
is marked as stale. The interface will
then reflect this.

Call the interface and compare 
the actual results with expected 
values. 

Send the actual values to the 
client in the client response. 

Error reporting is done in the 
data get and data set routines
and the specific compare tests
use INKDebug to report the error
as well as setting a global flag
that marks the test as fail.

Since the interface can only be 
called from the new hook  
INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK,
the resutls are initially stored in
the client request since the client 
response is not available at this point.

At a later hook where the client reponse
is available, the results are retrieved 
from the client request and copied to the 
client response.  

The code is static in the order that the 
values are stored/retrieve.

The final development approach has been to
start using this new interface in the most 
flexible way possible: by calling it at events
late in state machine processing (READ_RESPONSE)
and working out all the reading of data from
client request and returning results in the 
client response. 

This interface will be restricted so that it 
is only callable from the new INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK
where it will 
	1. require fewer test (test at one hook)
	2. harder to write test plug-in code since
	   the client response does not exist at this
	   time. 
		
	   Solution here is to store into client 
	   request at callable hook INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK	
	   and to retrieve and copy to client response 
	   at a later hook (probably READ_RESPONSE).


The format: 
	x-specific_mime_extension_hdr: Id=Value, ...

The test plug-in requires all 4 input values:
x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_MISS
x-expected_call_result: result=1
x-expected_lookup_count: value=1
x-expected_test_result: result=pass


INKHttpTxnCacheLookupStatusGet

----------------------------------------------------
	GET http://www.inktomi.com/ HTTP/1.0
	x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
	x-expected_call_result: result=1
	x-expected_test_result: result=pass
	x-expected_lookup_count: value=1
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_MISS

	HTTP/1.0 200 OK
	Server: Netscape-Enterprise/4.1
	Date: Fri, 08 Dec 2000 05:06:14 GMT
	Content-type: text/html
	Age: 0
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
	x-actual_call_result: got=1
	x-actual_lookup_count: got=1
	x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet



Multiple lookup test performs as many lookup as lookup_count 
value is set to.
------------------------------------------------

	GET http://www.inktomi.com/ HTTP/1.0
	x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
	x-expected_call_result: result=1
	x-expected_test_result: result=pass
	x-expected_lookup_count: value=2
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_MISS

	HTTP/1.0 200 OK
	Server: Netscape-Enterprise/4.1
	Date: Sat, 09 Dec 2000 05:42:01 GMT
	Content-type: text/html
	Age: 0
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
	x-actual_call_result: got=1
	x-actual_lookup_count: got=2
	x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet



LOOKUP_HIT_FRESH test: expects cache lookup status of INK_CACHE_LOOKUP_HIT_FRESH
-----------------------
	Trying 209.131.48.213...
	Connected to npdev.inktomi.com.
	Escape character is '^]'.
	GET http://www.inktomi.com/ HTTP/1.0
	x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
	x-expected_call_result: result=1
	x-expected_test_result: result=pass
	x-expected_lookup_count: value=5
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_FRESH

	HTTP/1.0 200 OK
	Server: Netscape-Enterprise/4.1
	Date: Tue, 12 Dec 2000 15:52:22 GMT
	Content-type: text/html
	Age: 56
	Content-Length: 15866
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cHs f ])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_HIT_FRESH
	x-actual_call_result: got=1
	x-actual_lookup_count: got=5
	x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet




LOOKUP_MISS  test expects a cache lookup status of INK_CACHE_LOOKUP_MISS.
---------------------
	GET http://www.hns.com HTTP/1.0
	x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
	x-expected_call_result: result=1
	x-expected_test_result: result=pass
	x-expected_lookup_count: value=5
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_MISS

	HTTP/1.0 200 OK
	Server: Netscape-Enterprise/3.6 SP2
	Date: Tue, 19 Dec 2000 17:06:16 GMT
	Content-type: text/html
	Last-modified: Fri, 08 Dec 2000 17:13:54 GMT
	Content-length: 19231
	Accept-ranges: bytes
	Age: 38261
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
	x-actual_call_result: got=1
	x-actual_lookup_count: got=5
	x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet


LOOKUP_HIT_STALE test expects a cache lookup status of INK_CACHE_LOOKUP_HIT_STALE 
----------------------
	GET http://www.hns.com HTTP/1.0
	x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
	x-expected_call_result: result=1
	x-expected_test_result: result=pass
	x-expected_lookup_count: value=5
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_STALE

	HTTP/1.0 200 Ok
	Server: Netscape-Enterprise/3.6 SP2
	Date: Wed, 20 Dec 2000 03:45:49 GMT
	Content-type: text/html
	Last-modified: Fri, 08 Dec 2000 17:13:54 GMT
	Content-length: 19231
	Accept-ranges: bytes
	Age: 0
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cSsNfU])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_HIT_STALE
	x-actual_call_result: got=1
	x-actual_lookup_count: got=5
	x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet


MISC. TESTS
-----------
At the start of testing, it was thought that generating
a HIT_STALE would require additional steps. This tests
was noticed as an incidental HIT_STALE result. The 
test failed, but not as a failure in the interface. It
is included in these results mostly as an indication
that the test plug-in can handle all status results. 

	GET http://www.hns.com HTTP/1.0
	x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
	x-expected_call_result: result=1
	x-expected_test_result: result=pass
	x-expected_lookup_count: value=5
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_FRESH

	HTTP/1.0 200 Ok
	Server: Netscape-Enterprise/3.6 SP2
	Date: Wed, 20 Dec 2000 03:04:28 GMT
	Content-type: text/html
	Last-modified: Fri, 08 Dec 2000 17:13:54 GMT
	Content-length: 19231
	Accept-ranges: bytes
	Age: 0
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cSsNfU])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_HIT_STALE
	x-actual_call_result: got=1
	x-actual_lookup_count: got=5
	x-actual_test_result:  result=fail,  <exp:pass>=<actual:fail>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet


Negative case (incomplete test): 
An example of a test that failed due to missing
test parameters.

The test_result should be read as: expected fail, 
actual is fail, result is pass since the test
yielded the desired result.

--------------------------------
	GET http://www.inktomi.com HTTP/1.0
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_MISS
	x-expected_test_result: result=fail

	HTTP/1.0 200 OK
	Server: Netscape-Enterprise/4.1
	Date: Fri, 08 Dec 2000 05:02:00 GMT
	Content-type: text/html
	Age: 0
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
	x-actual_test_result:  result=pass,  <exp:fail>=<actual:fail>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet


---
Another example, this test is expected to fail since x-expected_test_result=fail 
and the failure is obviously due to the 
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_STALE 
on an initially empty cache.
<<<<<<< README.txt

GET http://www.inktomi.com/ HTTP/1.0
x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
x-expected_call_result: result=1
x-expected_test_result: result=fail 
x-expected_lookup_count: value=1
x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_STALE

HTTP/1.0 200 OK
Server: Netscape-Enterprise/4.1
Date: Fri, 08 Dec 2000 04:49:05 GMT
Content-type: text/html
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
x-actual_call_result: got=1
x-actual_lookup_count: got=1
x-actual_test_result:  result=pass,  <exp:fail>=<actual:fail>
x-api_interface_name: INKHttpTxnCacheLookupStatusGet



GET http://www.inktomi.com/ HTTP/1.0
x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_lookup_count: value=2
x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_MISS

HTTP/1.0 200 OK
Server: Netscape-Enterprise/4.1
Date: Sat, 09 Dec 2000 05:42:01 GMT
Content-type: text/html
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
x-actual_call_result: got=1
x-actual_lookup_count: got=2
x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
x-api_interface_name: INKHttpTxnCacheLookupStatusGet



Multiple lookup test passes:

Trying 209.131.48.213...
Connected to npdev.inktomi.com.
Escape character is '^]'.
GET http://www.inktomi.com/ HTTP/1.0
x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_lookup_count: value=5
x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_FRESH

HTTP/1.0 200 OK
Server: Netscape-Enterprise/4.1
Date: Tue, 12 Dec 2000 15:52:22 GMT
Content-type: text/html
Age: 56
Content-Length: 15866
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cHs f ])
x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_HIT_FRESH
x-actual_call_result: got=1
x-actual_lookup_count: got=5
x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
x-api_interface_name: INKHttpTxnCacheLookupStatusGet


GET http://www.hns.com HTTP/1.0
x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_lookup_count: value=5
x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_FRESH


HTTP/1.0 200 Ok
Server: Netscape-Enterprise/3.6 SP2
Date: Wed, 20 Dec 2000 03:04:28 GMT
Content-type: text/html
Last-modified: Fri, 08 Dec 2000 17:13:54 GMT
Content-length: 19231
Accept-ranges: bytes
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cSsNfU])
x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_HIT_STALE
x-actual_call_result: got=1
x-actual_lookup_count: got=5
x-actual_test_result:  result=fail,  <exp:pass>=<actual:fail>
x-api_interface_name: INKHttpTxnCacheLookupStatusGet
=======
>>>>>>> 1.5.4.7

	GET http://www.inktomi.com/ HTTP/1.0
	x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
	x-expected_call_result: result=1
	x-expected_test_result: result=fail 
	x-expected_lookup_count: value=1
	x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_STALE

	HTTP/1.0 200 OK
	Server: Netscape-Enterprise/4.1
	Date: Fri, 08 Dec 2000 04:49:05 GMT
	Content-type: text/html
	Age: 0
	Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
	x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
	x-actual_call_result: got=1
	x-actual_lookup_count: got=1
	x-actual_test_result:  result=pass,  <exp:fail>=<actual:fail>
	x-api_interface_name: INKHttpTxnCacheLookupStatusGet


<<<<<<< README.txt
MISS + HIT_STALE test 
---------------------

GET http://www.hns.com HTTP/1.0
x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_lookup_count: value=5
x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_MISS

HTTP/1.0 200 OK
Server: Netscape-Enterprise/3.6 SP2
Date: Tue, 19 Dec 2000 17:06:16 GMT
Content-type: text/html
Last-modified: Fri, 08 Dec 2000 17:13:54 GMT
Content-length: 19231
Accept-ranges: bytes
Age: 38261
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
x-actual_call_result: got=1
x-actual_lookup_count: got=5
x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
x-api_interface_name: INKHttpTxnCacheLookupStatusGet

=======
The test plan included using the config var: proxy.config.http.cache.heuristic_min/max_lifetime
to mark content as stale. 
>>>>>>> 1.5.4.7


<<<<<<< README.txt

GET http://www.hns.com HTTP/1.0
x-api_interface_name: name=INKHttpTxnCacheLookupStatusGet
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_lookup_count: value=5
x-expected_cache_lookup_status: status=INK_CACHE_LOOKUP_HIT_STALE

HTTP/1.0 200 Ok
Server: Netscape-Enterprise/3.6 SP2
Date: Wed, 20 Dec 2000 03:45:49 GMT
Content-type: text/html
Last-modified: Fri, 08 Dec 2000 17:13:54 GMT
Content-length: 19231
Accept-ranges: bytes
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cSsNfU])
x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_HIT_STALE
x-actual_call_result: got=1
x-actual_lookup_count: got=5
x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
x-api_interface_name: INKHttpTxnCacheLookupStatusGet



=======
>>>>>>> 1.5.4.7
INKHttpTxnRedirectRequest 
-------------------------

Same host:
---------
Trying 209.131.48.213...
Connected to npdev.inktomi.com.
Escape character is '^]'.
GET  http://www.intel.com HTTP/1.0
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_redirect_url: url=http://www.intel.com
x-expected_redirect_cnt: value=2

Response/Results
----------------
HTTP/1.0 200 OK
Server: Microsoft-IIS/4.0
Date: Wed, 20 Dec 2000 00:15:52 GMT
Set-Cookie: AnonymousGuest=01B6F6B2D60B11d4AC6D009027AEA3DA577; expires=Fri 28-May-2010 23:59:00 GMT; path=/;
Content-Type: text/html
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSf ])
x-actual_call_result:  got=1
x-actual_redirect_cnt:  value=2
x-actual_redirect_attempt:  value=2
x-actual_test_result:  result=pass
x-api_interface_name: INKHttpTxnRedirectRequest

977273176 17869 209.131.60.80 TCP_MISS/200 33501 GET http://www.intel.com - DIRECT/www.intel.com text/html



Different host:
--------------
GET  http://www.intel.com HTTP/1.0
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_redirect_url: url=http://www.hns.com
x-expected_redirect_cnt: value=2

HTTP/1.0 200 OK
Server: Microsoft-IIS/4.0
Date: Wed, 20 Dec 2000 00:37:27 GMT
Set-Cookie: AnonymousGuest=5303401CD60E11d4AC6A009027AEA3DC507; expires=Fri 28-May-2010 23:59:00 GMT; path=/;
Content-Type: text/html
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSf ])
x-actual_call_result:  got=1
x-actual_redirect_cnt:  value=2
x-actual_redirect_attempt:  value=2
x-actual_test_result:  result=pass
x-api_interface_name: INKHttpTxnRedirectRequest

squid.log
---------
977273068 4999 209.131.60.80 TCP_MISS/200 33501 GET http://www.hns.com - DIRECT/www.intel.com text/html


npdev> ./traffic_server -V                                
traffic_server 4.0.0-e - (build # 11198 on Dec 19 2000 at 08:16:59)



INKHttpTxnCacheLookupStatusSet

--------------------------------------
Approach based on format for extension: 
		x-specific_mime_extension_hdr: Id=Value, ...
REQ
---
GET http://npdev.inktomi.com:8080/index.html  HTTP/1.0
x-expected_call_result: result=1		
x-expected_test_result: result=pass
x-expected_get_cache_status: status=INK_CACHE_LOOKUP_MISS 
x-expected_set_cache_status: status=INK_CACHE_LOOKUP_MISS 

RESP
----
HTTP/1.0 200 OK
Date: Sat, 23 Dec 2000 00:24:15 GMT
Age: 11
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_call_result:  result=1
x-actual_get_cache_status:  status=INK_CACHE_LOOKUP_MISS
x-actual_test_result:  result=pass
x-api_interface_name: INKHttpTxnCacheLookupStatusSet


REQ
---
GET http://npdev.inktomi.com:8080/index.html HTTP/1.0
x-expected_call_result: result=1		
x-expected_test_result: result=pass
x-expected_get_cache_status: status=INK_CACHE_LOOKUP_HIT_FRESH 
x-expected_set_cache_status: status=INK_CACHE_LOOKUP_HIT_STALE

RESP
---
HTTP/1.0 200 Connection Established
Date: Sat, 23 Dec 2000 00:27:03 GMT
Via: HTTP/1.1 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cSs f ])
x-actual_call_result:  result=1
x-actual_get_cache_status:  status=INK_CACHE_LOOKUP_HIT_STALE
x-actual_test_result:  result=pass
x-api_interface_name: INKHttpTxnCacheLookupStatusSet


REQ
---
GET http://npdev.inktomi.com:8080/index.html HTTP/1.0
x-expected_call_result: result=1		
x-expected_test_result: result=pass
x-expected_get_cache_status: status=INK_CACHE_LOOKUP_HIT_FRESH
x-expected_set_cache_status: status=INK_CACHE_LOOKUP_MISS

RESP
---
HTTP/1.0 200 Connection Established
Date: Sat, 23 Dec 2000 00:31:12 GMT
Age: 28
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_call_result:  result=1
x-actual_get_cache_status:  status=INK_CACHE_LOOKUP_MISS
x-actual_test_result:  result=pass
x-api_interface_name: INKHttpTxnCacheLookupStatusSet


REQ
---
GET http://npdev.inktomi.com:8080/indexindex.html HTTP/1.0
x-expected_call_result: result=1		
x-expected_test_result: result=pass
x-expected_get_cache_status: status=INK_CACHE_LOOKUP_HIT_STALE 
x-expected_set_cache_status: status=INK_CACHE_LOOKUP_HIT_FRESH

REQ
---
GET http://www.inktomi.com HTTP/1.0
x-expected_call_result: result=1		
x-expected_test_result: result=pass
x-expected_get_cache_status: status=INK_CACHE_LOOKUP_HIT_STALE 
x-expected_set_cache_status: status=INK_CACHE_LOOKUP_MISS


Negative test:
-------------
REQ
---
GET http://www.inktomi.com HTTP/1.0
x-expected_call_result: result=1		
x-expected_test_result: result=pass
x-expected_get_cache_status: status=INK_CACHE_LOOKUP_MISS 
x-expected_set_cache_status: status=INK_CACHE_LOOKUP_HIT_FRESH


HTTP/1.0 200 OK
Server: Netscape-Enterprise/4.1
Date: Fri, 08 Dec 2000 05:06:14 GMT
Content-type: text/html
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_cache_lookup_status:  status=INK_CACHE_LOOKUP_MISS
x-actual_call_result: got=1
x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
x-api_interface_name: INKHttpTxnCacheLookupStatusGet


Results
------- 
[Dec 20 21:21:35.048] DEBUG: (http) [1] calling plugin on hook INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK at hook 0xC87570
[Dec 20 21:21:35.051] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 getTestParam: hdr = [x-expected_get_cache_status], val = [status=INK_CACHE_LOOKUP_MISS]

[Dec 20 21:21:35.054] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 getTestParam: hdr = [x-expected_set_cache_status], val = [status=INK_CACHE_LOOKUP_HIT_FRESH]

[Dec 20 21:21:35.058] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 getTestParam: hdr = [x-expected_test_result], val = [result=pass]

[Dec 20 21:21:35.060] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 getTestParam: hdr = [x-expected_call_result], val = [result=1]

[Dec 20 21:21:35.062] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 CacheLookupStatusSet: from [INK_CACHE_LOOKUP_MISS = 2] to [INK_CACHE_LOOKUP_HIT_FRESH = 2] 

[Dec 20 21:21:35.066] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 setTestResult:  added   [x-actual_call_result:  result=0] 

[Dec 20 21:21:35.069] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 setTestResult:  added   [x-actual_get_cache_status:  result=INK_CACHE_LOOKUP_MISS] 

[Dec 20 21:21:35.073] DIAG: (INKHttpTxnCacheLookupStatusSet) 
 setTestResult:  added   [x-actual_test_result:  result=fail] 


 -----------------------------------------------------------------------------
INKHttpTxnCacheLookupUrlGet 
(set to 1 CONFIG proxy.config.http.cache.cache_urls_that_look_dynamic INT 1)

npdev> ./traffic_server -V 
traffic_server 4.0.0-e - (build # 112111 on Dec 21 2000 at 11:20:00)

--------------------------
REQ:
---
GET http://www.inktomi.com/ HTTP/1.0
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_http_doc:    document=http://www.inktomi.com/

HTTP/1.0 200 OK
Server: Netscape-Enterprise/4.1
Date: Fri, 22 Dec 2000 22:16:18 GMT
Content-type: text/html
Age: 153
Content-Length: 15868
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cHs f ])
x-actual_call_result: got=1
x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
x-actual_http_doc: http://www.inktomi.com/
x-api_interface_name: INKHttpTxnCacheLookupUrlGet

REQ 
---
GET http://www.inktomi.com HTTP/1.0
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_http_doc:    document=http://www.inktomi.com

RESP (failure)
----
HTTP/1.0 200 OK
Server: Netscape-Enterprise/4.1
Date: Fri, 22 Dec 2000 22:16:18 GMT
Content-type: text/html
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_call_result: got=1
x-actual_test_result:  result=fail,  <exp:pass>=<actual:fail>
x-actual_http_doc: http://www.inktomi.com/
x-api_interface_name: INKHttpTxnCacheLookupUrlGet


REQ:
---
GET http://www.inktomi.com:80/index.html HTTP/1.0
x-expected_call_result: result=1
x-expected_test_result: result=fail
x-expected_http_doc:    document=http://www.foobar.com/

RESP
----
HTTP/1.0 200 OK
Server: Netscape-Enterprise/4.1
Date: Fri, 22 Dec 2000 22:33:34 GMT
Content-type: text/html
Age: 0
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_call_result: got=1
x-actual_test_result:  result=pass,  <exp:fail>=<actual:fail>
x-actual_http_doc: http://www.inktomi.com:80/index.html
x-api_interface_name: INKHttpTxnCacheLookupUrlGet


REQ
---
GET http://@npdev.inktomi.com:8080/cgi-bin/inventory?product=hammer43 HTTP/1.0
x-expected_call_result: result=1
x-expected_test_result: result=pass
x-expected_http_doc: 	document=http://joe:bolts4USA@npdev.inktomi.com:8080/cgi-bin/inventory?product=hammer43

RESP
----
HTTP/1.0 200 OK
Date: Fri, 22 Dec 2000 22:34:50 GMT
Age: 5
Via: HTTP/1.0 ink-proxy.inktomi.com (Traffic-Server/4.0.0 [cMsSfW])
x-actual_call_result: got=1
x-actual_test_result:  result=pass,  <exp:pass>=<actual:pass>
x-actual_http_doc: http://joe:bolts4USA@npdev.inktomi.com:8080/cgi-bin/inventory?product=hammer43
x-api_interface_name: INKHttpTxnCacheLookupUrlGet



403/RC1 
Location of the release candidate came from
/home/releng/np/release/candidates/traffic_tomcat_403.rc1/pkg/ts-403_di_alpha



Doc Notes 
 ------------------------------------------------------------------------------

NewCacheLookupDo: state that buff/loc need to be created prior to call. 
		How do the query strings affect cache lookup? 

