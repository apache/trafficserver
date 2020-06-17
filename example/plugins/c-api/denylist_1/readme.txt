How to run the denylist plugin
===============================

1. Modify denylist.cgi to specify the location of perl and traffic server.
2. Copy denylist.cgi, denylist_1.so, PoweredByInktomi.gif to the directory
   specified by the variable proxy.config.plugin.plugin_dir.
3. Modify plugin.config to load the denylist plugin.



About the denylist plugin
==========================

The denylist plugin allows Traffic Server to compare all incoming request
origin servers with a deny-listed set of web servers. If the requested origin
server is listed, Traffic Server sends the client a message saying that
access is denied.
