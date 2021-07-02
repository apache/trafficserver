.. Licensed to the Apache Software Foundation (ASF) under one or more
      contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at
   http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.
.. include:: /common.defs

.. default-domain:: c

TSSslSecretSet
**************

Set the data associated with a secret name specified in the config.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSSslSecretSet(const char * secret_name, int secret_name_length, const char * secret_data, int secret_data_len)

Description
===========

:func:`TSSslSecretSet` updates the current secret map. Generally the secret name corresponds to the name of a certificate or a key.
Future creation of SSL_CTX objects that use the secret will use the newly specified data. It can be useful to call this function
from the :cpp:enumerator:`TS_LIFECYCLE_SSL_SECRET_HOOK`.

TSSslSecretGet
**************

Get the data associated with a secret name specified in the config.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSSslSecretGet(const char * secret_name, int secret_name_length, const char ** secret_data_return, int * secret_data_len)

Description
===========

:func:`TSSslSecretGet` fetches the named secret from the current secret map. TS_ERROR is returned if there is no entry for the secret.

TSSslSecretUpdate
*****************

Tell |TS| to update the SSL objects dependent on the secret.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSSslSecretUpdate(const char * secret_name, int secret_name_length)

Description
===========

:func:`TSSslSecretUpdate` causes |TS| to update the SSL objects that depend on the specified secret.  This enables a plugin to look for
multiple secret updates and make calls to :func:`TSSslSecretSet` to update the secret table.  Then once everything is updated call
:func:`TSSslSecretUpdate` to update the SSL objects with a consistent updated set of secrets.
