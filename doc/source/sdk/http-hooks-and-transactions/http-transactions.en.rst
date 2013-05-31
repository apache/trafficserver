HTTP Transactions
*****************

.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at
 
   http://www.apache.org/licenses/LICENSE-2.0
 
  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.

The HTTP transaction functions enable you to set up plugin callbacks to
HTTP transactions and obtain/modify information about particular HTTP
transactions.

As described in the section on HTTP sessions, an **HTTP transaction** is
an object defined for the lifetime of a single request from a client and
the corresponding response from Traffic Server. The **``TSHttpTxn``**
structure is the main handle given to a plugin for manipulating a
transaction's internal state. Additionally, an HTTP transaction has a
reference back to the HTTP session that created it.

The sample code below illustrates how to register locally to a
transaction and associate data to the transaction.

::

    :::c
    /*
    * Simple plugin that illustrates:
    * - how to register locally to a transaction
    * - how to deal with data that's associated with a tranaction
    *
    * Note: for readability, error checking is omitted
    */

    #include <ts/ts.h>

    #define DBG_TAG "txn"

    /* Structure to be associated to txns */
    typedef struct {
       int i;
       float f;
       char *s;
    } TxnData;

    /* Allocate memory and init a TxnData structure */
    TxnData *
    txn_data_alloc()
    {
       TxnData *data;
       data = TSmalloc(sizeof(TxnData));
        
       data->i = 1;
       data->f = 0.5;
       data->s = "Constant String";
       return data;
    }
        
    /* Free up a TxnData structure */
    void
    txn_data_free(TxnData *data)
    {
       TSfree(data);
    }
        
    /* Handler for event READ_REQUEST and TXN_CLOSE */
    static int
    local_hook_handler (TSCont contp, TSEvent event, void *edata)
    {
       TSHttpTxn txnp = (TSHttpTxn) edata;
       TxnData *txn_data = TSContDataGet(contp);
       switch (event) {
       case TS_EVENT_HTTP_READ_REQUEST_HDR:
          /* Modify values of txn data */
          txn_data->i = 2;
          txn_data->f = 3.5;
          txn_data->s = "Constant String 2";
          break;
        
       case TS_EVENT_HTTP_TXN_CLOSE:
          /* Print txn data values */
          TSDebug(DBG_TAG, "Txn data i=%d f=%f s=%s", txn_data->i, txn_data->f, txn_data->s);
        
          /* Then destroy the txn cont and its data */
          txn_data_free(txn_data);
          TSContDestroy(contp);
          break;
        
       default:
           TSAssert(!"Unexpected event");
           break;
       }
        
       TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
       return 1;
    }
        
    /* Handler for event TXN_START */
    static int
    global_hook_handler (TSCont contp, TSEvent event, void *edata)
    {
       TSHttpTxn txnp = (TSHttpTxn) edata;
       TSCont txn_contp;
       TxnData *txn_data;
        
       switch (event) {
       case TS_EVENT_HTTP_TXN_START:
          /* Create a new continuation for this txn and associate data to it */
          txn_contp = TSContCreate(local_hook_handler, TSMutexCreate());
          txn_data = txn_data_alloc();
          TSContDataSet(txn_contp, txn_data);
        
          /* Registers locally to hook READ_REQUEST and TXN_CLOSE */
          TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, txn_contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);
          break;
        
       default:
          TSAssert(!"Unexpected event");
          break;
       }
        
       TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
       return 1;
    }
        
        
    void
    TSPluginInit (int argc, const char *argv[])
    {
       TSCont contp;
        
       /* Note that we do not need a mutex for this txn since it registers globally
          and doesn't have any data associated with it */
       contp = TSContCreate(global_hook_handler, NULL);
        
       /* Register gloabally */
       TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, contp);
    }

See `Adding Hooks <adding-hooks>`__ for background about HTTP
transactions and HTTP hooks, as well as `HTTP Hooks and
Transactions <../http-hooks-and-transactions>`__ Also see the [HTTP
Transaction State Diagram
](HTTPHooksAndTransactions.html(../http-hooks-and-transactions#HHTTPTxStateDiag)
for an illustration of the steps involved in a typical HTTP transaction.

The HTTP transaction functions are:

-  ```TSHttpTxnCacheLookupStatusGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad26c77fa4ba251fb8ccbbd1505a74687>`__

-  ```TSHttpTxnCachedReqGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a889b626142157077f4f3cfe479e8b8e2>`__
   - Note that it is an error to modify cached headers.

-  ```TSHttpTxnCachedRespGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ae8f24b8dabb5008ad11620a11682ffd6>`__
   - Note that it is an error to modify cached headers.

-  `TSHttpTxnClientIncomingPortGet <link/to/doxygen>`__

-  `TSHttpTxnClientIPGet <link/to/doxygen>`__

-  `TSHttpTxnClientRemotePortGet <link/to/doxygen>`__

-  ```TSHttpTxnClientReqGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#acca66f22d0f87bf8f08478ed926006a5>`__
   - Plugins that must read client request headers use this call to
   retrieve the HTTP header.

-  ```TSHttpTxnClientRespGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a92349c8363f72b1f6dfed3ae80901fff>`__

-  ```TSHttpTxnErrorBodySet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ad7efc431279dc97de4b50a58d4ed33c1>`__

-  ```TSHttpTxnHookAdd`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a10382b88145bbfba0fa9d8ed6402f4b1>`__

-  ```TSHttpTxnNextHopAddrGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aa0118beabfefe35d2642f007ac7afa97>`__

-  ```TSHttpTxnParentProxySet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a2a1260b900b665d38a262544446b886c>`__

-  ```TSHttpTxnReenable`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ac367347e02709ac809994dfb21d3288a>`__

-  ```TSHttpTxnServerAddrGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a63917ec11275c4f1ed559362865cd65f>`__

-  ```TSHttpTxnServerReqGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aac2343a8b47bf9150f3ff7cd4e692d57>`__

-  ```TSHttpTxnServerRespGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a39e8bfb199eadabb54c067ff25a9a400>`__

-  ```TSHttpTxnSsnGet`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a8c5190bd2e940ef2d1969a5be65f0edd>`__

-  ```TSHttpTxnTransformedRespCache`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a213b584cd04001e8f8ad509d187a4103>`__

-  ```TSHttpTxnTransformRespGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a4fc46502733adcff09587a436e300114>`__

-  ```TSHttpTxnUntransformedRespCache`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a8b9c0e61cbcb251417df0d06ae6c4408>`__


