.. _building-ats-for-transparency:

Building ATS for transparency
*****************************

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


In most cases, if your environment supports transparency then
``configure`` will automatically enable it. For other environments you
may need to twiddle the ``configure`` options.

``--enable-posix-cap``
    This enables POSIX capabilities, which are required for
    transparency. These are compiled in by default. To check your
    system, look for the header file ``sys/capability.h`` and the system
    library ``libcap``. These are in the packages ``libcap`` and
    ``libcap-devel`` or ``libcap-dev`` (depending on the Distribution)
    contra-respectively.

``--enable-tproxy[=value]``
    Enable TPROXY support, which is the Linux kernel feature used for
    transparency. This should be present in the base installation, there
    is no package associated with it. \* ``auto`` Do automatic checks
    for the the TPROXY header file (``linux/in.h``) and enable TPROXY
    support if the ``IP_TRANSPARENT`` definition is present. This is the
    default if this option is not specified or ``value`` is omitted. \*
    ``no`` Do not check for TPROXY support, disable support for it. \*
    ``force`` Do not check for TPROXY support, enable it using the $ats@
    built in value for ``IP_TRANSPARENT``. This is useful for systems
    that have it in the kernel for but some reason do not have the
    appropriate system header file. \* *number* Do not check for TPROXY
    support, use *number* as the ``IP_TRANSPARENT`` value. There are, at
    present, no known standard distributions of Linux that support
    TPROXY but use a value different from the built in ATS default.
    However, a custom built kernel may do so and in that case the
    specific value can be specified.

In the default case, ATS configuration will automatically check for
TPROXY support via the presence of the ``linux/in.h`` header file and
compile in TPROXY support if it is available. If that fails, you may be
able to recover by using one of the options above. Note that
transparency may be built in by default but it is not active unless
explicitly enabled in the ATS configuration files.

