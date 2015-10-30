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

.. include:: ../../../../common.defs

Working With HTTP Headers
*************************

The plugin checks all client request headers for the Proxy-Authorization
MIME field, which should contain the user name and password. The
plugin's continuation handler, ``auth-plugin``, calls ``handle_dns`` to
check the ``Proxy-Authorization`` field. The ``handle_dns`` routine uses
``TSHttpTxnClientReqGet`` and ``TSMimeHdrFieldFind`` to obtain the
``Proxy-Authorization`` field:

.. code-block:: c

    {
        TSMBuffer bufp;
        TSMLoc hdr_loc;
        TSMLoc field_loc;
        const char *val;
        char *user, *password;

        if (!TSHttpTxnClientReqGet (txnp, &bufp, &hdr_loc)) {
            TSError ("[basic_authorization] Couldn't retrieve client request header");
            goto done;
        }

        field_loc = TSMimeHdrFieldFind (bufp, hdr_loc,
                TS_MIME_FIELD_PROXY_AUTHORIZATION);

If the ``Proxy-Authorization`` field is present, then the plugin checks
that the authentication type is "Basic", and the user name and password
are present and valid:

.. code-block:: c

    val = TSMimeHdrFieldValueStringGet (bufp, hdr_loc, field_loc, -1, &authval_length);
    if (!val) {
        TSError ("[basic_authorization] No value in Proxy-Authorization field");
        TSHandleMLocRelease (bufp, hdr_loc, field_loc);
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }

    if (strncmp (val, "Basic", 5) != 0) {
        TSError ("[basic_authorization] No Basic auth type in Proxy-Authorization");
        TSHandleMLocRelease (bufp, hdr_loc, field_loc);
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }

    val += 5;
    while ((*val == ' ') || (*val == '\t')) {
        val += 1;
    }

    user = base64_decode (val);
    password = strchr (user, ':');
    if (!password) {
        TSError ("[basic_authorization] No password in authorization information");
        TSfree (user);
        TSHandleMLocRelease (bufp, hdr_loc, field_loc);
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }
    *password = '\0';
    password += 1;

    if (!authorized (user, password)) {
        TSError ("[basic_authorization] %s:%s not authorized", user, password);
        TSfree (user);
        TSHandleMLocRelease (bufp, hdr_loc, field_loc);
        TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
        goto done;
    }

    TSfree (user);
    TSHandleMLocRelease (bufp, hdr_loc, field_loc);
    TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable (txnp, TS_EVENT_HTTP_CONTINUE);
    return;

