About add-header.c

This plugin adds a header to a request.

Enter the text of the header to be added into the plugin.config file; for 
example,
enter the following line in plugin.config

	On NT:	AddHeader.dll "name1: value1" "name2: value2"
	On Solaris: add-header.so "name1: value1" "name2: value2"

The INKPluginInit function does the following:

- creates a MIME field buffer that contains the header to be added, 
	using the following functions:
	INKMBufferCreate
	INKMimeHdrCreate
	INKMimeHdrFieldCreate
	INKMimeFieldInsert
	INKMimeHdrFieldNameSet
	INKMimeFieldValueInsert
	

- sets up the callback for the add-header-plugin function, which 
	is the main callback function, using 
	INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK,
	INKContCreate(add_header_plugin, NULL);

add_header_plugin is the main function in the plugin. In the 
event of INK_EVENT_HTTP_READ_REQUEST_HDR (when the HTTP
state machine reads a request header), it calls the function
add_header. 

add_header first makes sure that it can retrieve the client request
header from the current transaction, using 
	INKHttpTxnClientReqGet

 copies the header into the MIME headers of the 
HTTP request, using the following functions:

	INKMimeHdrFieldGet
	INKMimeHdrFieldCreate
	INKMimeHdrFieldCopy
	INKMimeHdrFieldAppend
	INKMimeHdrFieldNext

When add_header is done, it uses
	INKHttpTxnReenable 
to continue. 


EXAMPLE
-------
One example of adding to a request header is to put this line into 
the plugins.config: 

	add-header.deprecated.so "Pragma: no-cache" 

Make sure that the page is in the cache by using the cache inspector UI. Add
this to records.config:

	CONFIG proxy.config.http_ui_enabled INT 1

reread the config by restarting traffic server or by running traffic_line -x. 
Configure your browser to connect to your proxy server. 
In IE: 
tools->internet options -> Connections -> LAN Settings... -> Use a proxy server. 
Once configured, open a browser to:

	http:{cache} 

Type in the URL that you requested. Stats on the page should be displayed. If
it's not in the cache then issue a GET http://www.yourpage.com HTTP/1.0 and
recheck that the page is in the cache.


The "Prage: no-cache"  directive will be read by traffic server and should 
show some interesting results in the squid.log: 

	968377769 3003 209.131.60.80 TCP_REFRESH_MISS/200 11115 
		GET http://www.inktomi.com/ - DIRECT/www.inktomi.com text/html

Any name-value pair can be used as an argument to the plug-on, but this is
perhaps a more real-world example of adding to the header of a request.


