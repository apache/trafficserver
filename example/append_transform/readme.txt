About append_transform.c

This example is based on null_transform.c. It appends text to the body
of an HTML text response document on its way from the cache to the
client.

The plugin first makes sure that it has received a "200 OK"
response from the origin server.  It then verifies that the returned
document is of type "text/html".  It "transforms" the document by
appending text to the file.   To read and write to the body of the
document, the plugin uses functions similar to those in null_transform.c.

You place the text to be appended in a file, and you must provide the
path to the file in plugin.config. In other words, plugin.config must
have a line similar to the following:

  append_transform.so path/to/file

Specify an absolute path to the text file, or a relative path as
described in the file-plugin README.

TSPluginInit does the following:

- makes sure that there is a text file specified in plugin.config
	(if not, it returns an error message)

- calls the function load to load the contents of the file into
	a buffer to be appended to HTML response bodies

- sets up the global hook to call back the plugin:
	TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK,
TSContCreate (transform_plugin, NULL));

The load function does the following (similar to file-1.c):

- opens the file specified in plugin.config using
	TSfopen

- creates a buffer for the text to be appended using
	TSIOBufferCreate

- creates a buffer reader for the text to be appended using
	TSIOBufferReaderAlloc

- reads the contents of the file in to the buffer using:
	TSIOBufferStart
	TSIOBufferBlockWriteStart
	TSfread
	TSIOBufferProduce
	TSIOBufferReaderAvail

- closes the file using
	TSfclose

The transform_plugin function does the following:

- tests the response body to make sure it is text/html, using
	the function "transformable". The transformable function
	uses the following API calls:
	TSHttpTxnServerRespGet
	TSHttpHdrStatusGet
	TSMimeHdrFieldFind
	TSMimeHdrFieldValueStringGet

- if the response body is deemed transformable, transform_plugin calls
	transform_add

- continues the HTTP transaction using
	TSHttpTxnReenable

The transform_add function does the following:

- creates a continuation for the append transform, using
	TSTransformCreate (append_transform, txnp);
	The handler function for this continuation is
	append_transform.

- adds a transaction hook for the append transform, using
	TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, connp);
	This transaction hook sets up a callback during transactions.
	When the event TS_HTTP_RESPONSE_TRANSFORM_HOOK happens,
	the append_transform function is called back.

The remaining functions in the plugin, append_transform and
handle_transform, are similar to null_transform and
handle_transform in the null_transform.c plugin.


