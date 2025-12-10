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

.. default-domain:: cpp

TSConnectionLimitExemptList
===========================

Synopsis
--------

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSConnectionLimitExemptListAdd(std::string_view ip_ranges)
.. function:: TSReturnCode TSConnectionLimitExemptListRemove(std::string_view ip_ranges)
.. function:: void TSConnectionLimitExemptListClear()

Description
-----------

These functions manage the per-client connection limit exempt list, which contains IP addresses
and ranges that are exempt from the connection limits enforced by
:ts:cv:`proxy.config.net.per_client.max_connections_in`.

:func:`TSConnectionLimitExemptListAdd` adds one or more IP addresses or CIDR ranges specified in
:arg:`ip_ranges` to the existing exempt list. The :arg:`ip_ranges` parameter can be a single
IP address or CIDR range, or a comma-separated string of multiple ranges (e.g.,
"192.168.1.10,10.0.0.0/8,172.16.0.0/12"). The ranges are added without removing any existing
entries. Returns :enumerator:`TS_SUCCESS` if all ranges were successfully added, :enumerator:`TS_ERROR` if
any of the IP ranges are invalid or if the operation fails.

:func:`TSConnectionLimitExemptListRemove` removes one or more IP addresses or CIDR ranges specified in
:arg:`ip_ranges` from the existing exempt list. The :arg:`ip_ranges` parameter can be a single
IP address or CIDR range, or a comma-separated string of multiple ranges. If a range is not present
in the list, it is silently ignored. Returns :enumerator:`TS_SUCCESS` if all ranges were successfully
processed, :enumerator:`TS_ERROR` if any of the IP ranges are invalid or if the operation fails.

:func:`TSConnectionLimitExemptListClear` removes all entries from the per-client connection
limit exempt list. After calling this function, all clients will be subject to connection
limits. This function does not return a value and never fails.

All functions are thread-safe and can be called from any plugin context. Changes made through
these functions will override any configuration set via
:ts:cv:`proxy.config.http.per_client.connection.exempt_list`.

Return Values
-------------

:func:`TSConnectionLimitExemptListAdd` and :func:`TSConnectionLimitExemptListRemove` return
:enumerator:`TS_SUCCESS` if the operation completed successfully, or :enumerator:`TS_ERROR` if the
operation failed due to invalid input or system errors.

Examples
--------

.. code-block:: cpp

    #include <ts/ts.h>
    #include <fstream>
    #include <string>

    void load_exempt_list_from_file(const char *filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            TSError("Failed to open exempt list file: %s", filename);
            return;
        }

        // Clear existing exempt list before loading from file
        TSConnectionLimitExemptListClear();

        std::string line;
        int line_num = 0;
        while (std::getline(file, line)) {
            line_num++;

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // Add each IP range to the exempt list
            TSReturnCode result = TSConnectionLimitExemptListAdd(line.c_str());
            if (result != TS_SUCCESS) {
                TSError("Failed to add IP range '%s' from line %d in %s", line.c_str(), line_num, filename);
            } else {
                TSDebug("exempt_list", "Added IP range: %s", line.c_str());
            }
        }
        file.close();
    }

    void TSPluginInit(int argc, const char *argv[]) {
        const char *exempt_file = "exempt_ips.txt";

        // Check if custom file specified in plugin arguments
        if (argc > 1) {
            exempt_file = argv[1];
        }

        // Load exempt list from file
        load_exempt_list_from_file(exempt_file);
    }


See Also
--------

:ts:cv:`proxy.config.net.per_client.max_connections_in`,
:ts:cv:`proxy.config.http.per_client.connection.exempt_list`
