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
	INKMimeFieldCreate
	INKMimeFieldInsert
	INKMimeFieldNameSet
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
	INKMimeFieldCreate
	INKMimeFieldCopy
	INKMimeHdrFieldInsert
	INKMimeFieldNext

When add_header is done, it uses
	INKHttpTxnReenable 
to continue. 

