How to run the blacklist plugin
===============================

1. Modify blacklist.cgi to specify the location of perl and traffic server.
2. Copy blacklist.cgi, blacklist_1.so, PoweredByInktomi.gif to the directory
   specified by the variable proxy.config.plugin.plugin_dir.
3. Modify plugin.config to load the blacklist plugin.



About the blacklist plugin
==========================

The blacklist plugin allows Traffic Server to compare all incoming request
origin servers with a blacklisted set of web servers. If the requested origin
server is blacklisted, Traffic Server sends the client a message saying that
access is denied.
