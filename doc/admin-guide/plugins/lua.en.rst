ts.connection_limit_exempt_list_add
------------------------------------
**syntax:** *success = ts.connection_limit_exempt_list_add(IP_RANGES)*

**context:** global

**description**: Add IP ranges to the per-client connection limit exempt list. This function wraps the TSConnectionLimitExemptListAdd API.

The IP_RANGES parameter should be a string containing one or more IP address ranges in CIDR notation, separated by commas. Client connections from these IP ranges will be exempt from per-client connection limits.

Returns true on success, false on failure.

Here is an example:

::

       if ts.connection_limit_exempt_list_add('10.0.0.0/8,192.168.1.0/24') then
           ts.debug('Successfully added IP ranges to exempt list')
       else
           ts.error('Failed to add IP ranges to exempt list')
       end

:ref:`TOP <admin-plugins-ts-lua>`

ts.connection_limit_exempt_list_remove
---------------------------------------
**syntax:** *success = ts.connection_limit_exempt_list_remove(IP_RANGES)*

**context:** global

**description**: Remove IP ranges from the per-client connection limit exempt list. This function wraps the TSConnectionLimitExemptListRemove API.

The IP_RANGES parameter should be a string containing one or more IP address ranges in CIDR notation, separated by commas.

Returns true on success, false on failure.

Here is an example:

::

       if ts.connection_limit_exempt_list_remove('192.168.1.0/24') then
           ts.debug('Successfully removed IP range from exempt list')
       else
           ts.error('Failed to remove IP range from exempt list')
       end

:ref:`TOP <admin-plugins-ts-lua>`

ts.connection_limit_exempt_list_clear
--------------------------------------
**syntax:** *ts.connection_limit_exempt_list_clear()*

**context:** global

**description**: Clear all IP ranges from the per-client connection limit exempt list. This function wraps the TSConnectionLimitExemptListClear API.

This function removes all entries from the exempt list.

Here is an example:

::

       ts.connection_limit_exempt_list_clear()
       ts.debug('Cleared connection limit exempt list')

:ref:`TOP <admin-plugins-ts-lua>`
