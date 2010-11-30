The order plugins help determine if the order in which the plugins are invoked at all hooks are in the sequence in which they are listed in the plugin.config file.

The order plugins are a set of 5 plugin files viz. orderstartplugin.c, orderplugin1.c, orderplugin2.c, orderplugin3.c and orderplugin4.c

The orderstartplugin.c has to be listed at the top in plugin.config file. It initializes the process of checking the sequence of invoking the plugins. It adds a counter to the header of each request and if it is present, then it initializes the counter to zero.

The rest of the plugins viz. orderplugin1.c, orderplugin2.c, orderplugin3.c and orderplugin4.c can be listed in any order in the plugin.config file, but the parameter passed to the plugins should be in the order of their listings. An e.g. of the listing is as follows:

orderplugin4.c 1
orderplugin1.c 2
orderplugin3.c 3
orderplugin4.c 4


The parameter passed to the plugin should be its position with respective to the other plugins in the plugin.config file.

These plugins gets the counter from the header, checks if it is invoked in the correct sequence and then updates the counter. The above plugins are added to the following hooks:

TS_HTTP_READ_REQUEST_HDR_HOOK
TS_HTTP_OS_DNS_HOOK
TS_HTTP_SEND_REQUEST_HDR_HOOK
TS_HTTP_READ_CACHE_HDR_HOOK
TS_HTTP_READ_RESPONSE_HDR_HOOK
TS_HTTP_SEND_RESPONSE_HDR_HOOK

At each hook the sequence of invoking the plugins is checked. If the plugins are not invoked in the sequnce of their listing in plugin.config file, then an error message is logged in logs/error.log file.

The assumption in these plugins is that only one plugin is attached to all hooks from one .so/.dll file. To a specific hook either all or none of the plugins are attached.

 
