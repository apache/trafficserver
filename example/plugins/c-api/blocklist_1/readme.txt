How to run the blocklist plugin
===============================

1. Modify blocklist.cgi to specify the location of perl and traffic server.
2. Copy blocklist.cgi, blocklist_1.so, PoweredByInktomi.gif to the directory
   specified by the variable proxy.config.plugin.plugin_dir.
3. Modify plugin.config to load the blocklist plugin.



About the blocklist plugin
==========================

The blocklist plugin allows Traffic Server to compare all incoming request
origin servers with a blocklisted set of web servers. If the requested origin
server is blocklisted, Traffic Server sends the client a message saying that
access is denied.
