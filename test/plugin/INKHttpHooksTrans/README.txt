
Test plan for Ch. 6 Programmers Guilde


INKHttpHook.c
------------
Tests for "global" hooks, i.e., registering for events not at the session
or transaction level and processing those events.


INKHttpSsnHookAdd.c
------------------ 
Tests for registering/processing events at the session level. 

Currently, only have INK_HTTP_REQUEST_TRANSFORM_HOOK. 
TODO test that transforms the response INK_HTTP_RESPONSE_TRANSFORM_HOOK


INKHttpTxnHookAdd.c
-------------------
Tests for registering/processing:

	INK_HTTP_SESSION_START
	INK_HTTP_TXN_START 
	INK_HTTP_SESSION_CLOSE
	INK_HTTP_TXN_CLOSE

events at the transaction level.
The test starts by registering and receiving events at the session
level and then registering events at the transaction level.


INKHttpSelAlt.c
---------------
TODO tests that register, receive and process event INK_HTTP_SELECT_ALT_HOOK
This test was written as a stand-alone plug-in since there appeared to be 
interactions with other events that interfered with this event. Once this 
code works, it could be incorporated into the INKHttpTxn.c plug-in since this 
is a test of "global" hook/event processing.

Notes altSelect.notes indicate how such a plug-in can be written. 


INKHttpReenableStop.c
--------------------
TODO tests of stopping an event at the session and transaction level


INKHttpTxnServer.c
------------------
Test for origin server interaction: 
	INKHttpTxnServerReqGet
	INKHttpTxnServerRespGet
and one misc Session level interface: 
	INKHttpTxnSsnGet

Delayed due to: INKqa08306. 


INKHttpTxnErrBodySet.c
----------------------
Implements setting the response
body on error.


INKHttpTxnIPAddress.cc
--------------------- 
Tests of retrieving IP address 
for a given transaction.


INKHttpTransaction.c
--------------------
Currently unused. 




----------------------------------------------------------------------------

Debug tag: INKHttpHook and INKHttpSsnHookAdd 

----------------------------------------------------------------------------
INKHttpSsnHookAdd.c: 

Summary
CACHE MISS

ChkEvents: -- INK_EVENT_HTTP_SSN_START -- 
ChkEvents: -- INK_EVENT_HTTP_TXN_START -- 
ChkEvents: -- INK_EVENT_HTTP_READ_REQUEST_HDR -- 
ChkEvents: -- INK_EVENT_HTTP_OS_DNS -- 
ChkEvents: -- INK_EVENT_HTTP_SEND_REQUEST_HDR -- 
ChkEvents: -- INK_EVENT_HTTP_READ_RESPONSE_HDR -- 
ChkEvents: -- INK_EVENT_HTTP_SEND_RESPONSE_HDR -- 
ChkEvents: -- INK_EVENT_HTTP_SSN_CLOSE -- 
ChkEvents: -- INK_EVENT_HTTP_TXN_CLOSE -- 
ChkEvents: -- INK_EVENT_HTTP_RESPONSE_TRANSFORM -- 
Event [0] INK_EVENT_HTTP_CONTINUE registered and not called back
Event [1] INK_EVENT_HTTP_ERROR registered and not called back
Event [5] INK_EVENT_HTTP_READ_CACHE_HDR registered and not called back
Event [8] INK_EVENT_HTTP_REQUEST_TRANSFORM registered and not called back
Event [10] INK_EVENT_HTTP_SELECT_ALT registered and not called back
Event [15] INK_EVENT_MGMT_UPDATE registered and not called back

CACHE HIT

ChkEvents: -- INK_EVENT_HTTP_SSN_START -- 
ChkEvents: -- INK_EVENT_HTTP_TXN_START -- 
ChkEvents: -- INK_EVENT_HTTP_READ_REQUEST_HDR -- 
ChkEvents: -- INK_EVENT_HTTP_OS_DNS -- 
ChkEvents: -- INK_EVENT_HTTP_SELECT_ALT -- 
ChkEvents: -- INK_EVENT_HTTP_READ_CACHE_HDR -- 
ChkEvents: -- INK_EVENT_HTTP_SEND_RESPONSE_HDR -- 
ChkEvents: -- INK_EVENT_HTTP_SSN_CLOSE -- 
ChkEvents: -- INK_EVENT_HTTP_TXN_CLOSE -- 
+OK: Event [0] INK_EVENT_HTTP_CONTINUE registered and not called back
+OK: Event [1] INK_EVENT_HTTP_ERROR registered and not called back
*OK: Event [8] INK_EVENT_HTTP_REQUEST_TRANSFORM registered and not called back
++OK: Event [15] INK_EVENT_MGMT_UPDATE registered and not called back
-------

+OK: These events are listed here for completeness but are not events 
that a plug-in would register or receive.

*OK: Not yet implimented.
Event INK_EVENT_HTTP_REQUEST_TRANSFORM should be covered in INKHttpSelAlt.c 
but this plug-in is waiting input from devel.

++OK: Covered in the blacklist plugin.  Further work should excercise this
plugin here.


INK_HTTP_SELECT_ALT_HOOK (received) 
----------------------------------------------------------------------------
If INK_HTTP_SELECT_ALT_HOOK is not registered at the global level, event 
is never received. This is documented.


