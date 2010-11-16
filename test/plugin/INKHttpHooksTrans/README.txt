
Test plan for Ch. 6 Programmers Guilde


TSHttpHook.c
------------
Tests for "global" hooks, i.e., registering for events not at the session
or transaction level and processing those events.


TSHttpSsnHookAdd.c
------------------ 
Tests for registering/processing events at the session level. 

Currently, only have TS_HTTP_REQUEST_TRANSFORM_HOOK. 
TODO test that transforms the response TS_HTTP_RESPONSE_TRANSFORM_HOOK


TSHttpTxnHookAdd.c
-------------------
Tests for registering/processing:

	TS_HTTP_SESSION_START
	TS_HTTP_TXN_START 
	TS_HTTP_SESSION_CLOSE
	TS_HTTP_TXN_CLOSE

events at the transaction level.
The test starts by registering and receiving events at the session
level and then registering events at the transaction level.


TSHttpSelAlt.c
---------------
TODO tests that register, receive and process event TS_HTTP_SELECT_ALT_HOOK
This test was written as a stand-alone plug-in since there appeared to be 
interactions with other events that interfered with this event. Once this 
code works, it could be incorporated into the TSHttpTxn.c plug-in since this 
is a test of "global" hook/event processing.

Notes altSelect.notes indicate how such a plug-in can be written. 


TSHttpReenableStop.c
--------------------
TODO tests of stopping an event at the session and transaction level


TSHttpTxnServer.c
------------------
Test for origin server interaction: 
	TSHttpTxnServerReqGet
	TSHttpTxnServerRespGet
and one misc Session level interface: 
	TSHttpTxnSsnGet

Delayed due to: TSqa08306. 


TSHttpTxnErrBodySet.c
----------------------
Implements setting the response
body on error.


TSHttpTxnIPAddress.cc
--------------------- 
Tests of retrieving IP address 
for a given transaction.


TSHttpTransaction.c
--------------------
Currently unused. 




----------------------------------------------------------------------------

Debug tag: TSHttpHook and TSHttpSsnHookAdd 

----------------------------------------------------------------------------
TSHttpSsnHookAdd.c: 

Summary
CACHE MISS

ChkEvents: -- TS_EVENT_HTTP_SSN_START -- 
ChkEvents: -- TS_EVENT_HTTP_TXN_START -- 
ChkEvents: -- TS_EVENT_HTTP_READ_REQUEST_HDR -- 
ChkEvents: -- TS_EVENT_HTTP_OS_DNS -- 
ChkEvents: -- TS_EVENT_HTTP_SEND_REQUEST_HDR -- 
ChkEvents: -- TS_EVENT_HTTP_READ_RESPONSE_HDR -- 
ChkEvents: -- TS_EVENT_HTTP_SEND_RESPONSE_HDR -- 
ChkEvents: -- TS_EVENT_HTTP_SSN_CLOSE -- 
ChkEvents: -- TS_EVENT_HTTP_TXN_CLOSE -- 
ChkEvents: -- TS_EVENT_HTTP_RESPONSE_TRANSFORM -- 
Event [0] TS_EVENT_HTTP_CONTINUE registered and not called back
Event [1] TS_EVENT_HTTP_ERROR registered and not called back
Event [5] TS_EVENT_HTTP_READ_CACHE_HDR registered and not called back
Event [8] TS_EVENT_HTTP_REQUEST_TRANSFORM registered and not called back
Event [10] TS_EVENT_HTTP_SELECT_ALT registered and not called back
Event [15] TS_EVENT_MGMT_UPDATE registered and not called back

CACHE HIT

ChkEvents: -- TS_EVENT_HTTP_SSN_START -- 
ChkEvents: -- TS_EVENT_HTTP_TXN_START -- 
ChkEvents: -- TS_EVENT_HTTP_READ_REQUEST_HDR -- 
ChkEvents: -- TS_EVENT_HTTP_OS_DNS -- 
ChkEvents: -- TS_EVENT_HTTP_SELECT_ALT -- 
ChkEvents: -- TS_EVENT_HTTP_READ_CACHE_HDR -- 
ChkEvents: -- TS_EVENT_HTTP_SEND_RESPONSE_HDR -- 
ChkEvents: -- TS_EVENT_HTTP_SSN_CLOSE -- 
ChkEvents: -- TS_EVENT_HTTP_TXN_CLOSE -- 
+OK: Event [0] TS_EVENT_HTTP_CONTINUE registered and not called back
+OK: Event [1] TS_EVENT_HTTP_ERROR registered and not called back
*OK: Event [8] TS_EVENT_HTTP_REQUEST_TRANSFORM registered and not called back
++OK: Event [15] TS_EVENT_MGMT_UPDATE registered and not called back
-------

+OK: These events are listed here for completeness but are not events 
that a plug-in would register or receive.

*OK: Not yet implimented.
Event TS_EVENT_HTTP_REQUEST_TRANSFORM should be covered in TSHttpSelAlt.c 
but this plug-in is waiting input from devel.

++OK: Covered in the blacklist plugin.  Further work should excercise this
plugin here.


TS_HTTP_SELECT_ALT_HOOK (received) 
----------------------------------------------------------------------------
If TS_HTTP_SELECT_ALT_HOOK is not registered at the global level, event 
is never received. This is documented.


