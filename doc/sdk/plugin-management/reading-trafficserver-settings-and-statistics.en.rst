Reading Traffic Server Settings and Statistics
**********************************************

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

Your plugin might need to know information about Traffic Server's
current configuration and performance. The functions described in this
section read this information from the Traffic Server :file:`records.config`
file. Configuration settings are stored in ``CONFIG`` variables and
statistics are stored in ``PROCESS`` variables.

.. caution::

   Not all ``CONFIG`` and ``PROCESS`` variables in :file:`records.config` are
   relevant to Traffic Server's configuration and statistics. Therefore,
   retrieve only the :file:`records.config` variables that are documented in
   the :doc:`Traffic Server Administrator's Guide <../../admin/index.en>`.

To retrieve a variable, you need to know its type (``int``, ``counter``,
``float``, or ``string``). Plugins store the :file:`records.config` values
as an ``TSMgmtInt``, ``TSMgmtCounter``, ``TSMgmtFloat``, or
``TSMgmtString``. You can look up :file:`records.config` variable types in
the :doc:`Traffic Server Administrator's Guide <../../admin/index.en>`.

Depending on the result type, you'll use ``TSMgmtIntGet``,
``TSMgmtCounterGet``, ``TSMgmtFloatGet``, or ``TSMgmtStringGet`` to
obtain the variable value (see the example for
:c:func:`TSMgmtIntGet`.

The ``TSMgmt*Get`` functions are:

-  :c:func:`TSMgmtCounterGet`

-  :c:func:`TSMgmtFloatGet`

-  :c:func:`TSMgmtIntGet`

-  :c:func:`TSMgmtStringGet`


