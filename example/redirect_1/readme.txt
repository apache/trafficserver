About redirect_1.c

This plugin redirects requests from a specified IP address ("block_ip")
to a specified URL ("url_redirect").

Specify block_ip and url_redirect int the plugin.config file; for
example, enter the following line in plugin.config

	redirect_1.so block_ip url_redirect
