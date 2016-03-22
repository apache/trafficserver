.. _admin-plugins-epic:

Epic Plugin
***********

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

The `Epic` plugin emits Traffic Server metrics in a format that is
consumed by the `Epic Network Monitoring System
<https://code.google.com/p/epicnms/>`_.  It is a global plugin that
is installed by adding it to the :file:`plugin.config` file.

Plugin Options
--------------

--directory=DIR
  Specify the directory the plugin will write sample files to. The
  default is ``/usr/local/epic/cache/eapi``.

--period=SECS
  Specify the sample period in seconds. The default is to write samples every
  30 seconds.

Caveats
-------

The Traffic Server metrics system does not store the semantics of
metrics, so it is not possible to programmatically determine whether
a metrics can be rate converted. The plugin contains a static list
of metrics that should not be rate converted (gauges in Epic
terminilogy).
