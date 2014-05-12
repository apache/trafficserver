.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License") you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.

.. default-domain:: c

=====================
TSTextLogObjectCreate
=====================

Traffic Server text logging API.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSTextLogObjectCreate(const char* filename, int mode, TSTextLogObject * new_log_obj)
.. function:: TSReturnCode TSTextLogObjectWrite(TSTextLogObject the_object, const char * format, ...)
.. function:: void TSTextLogObjectFlush(TSTextLogObject the_object)
.. function:: TSReturnCode TSTextLogObjectDestroy(TSTextLogObject the_object)
.. function:: void TSTextLogObjectHeaderSet(TSTextLogObject the_object, const char * header)
.. function:: TSReturnCode TSTextLogObjectRollingEnabledSet(TSTextLogObject the_object, int rolling_enabled)
.. function:: void TSTextLogObjectRollingIntervalSecSet(TSTextLogObject the_object, int rolling_interval_sec)
.. function:: void TSTextLogObjectRollingOffsetHrSet(TSTextLogObject the_object, int rolling_offset_hr)

Description
===========

:func:`TSTextLogObjectRollingEnabledSet` sets the log rolling mode
for the given object. This API must be used once the object is
created and before writing into logs. The :arg:`rolling_enabled`
argument must be a valid :ts:cv:`proxy.config.log.rolling_enabled`
values. If :func:`TSTextLogObjectRollingEnabledSet` is never called,
the log object takes it's log rolling mode from the global
:ts:cv:`proxy.config.log.rolling_enabled` setting.

See also
========

:manpage:`TSAPI(3ts)`
