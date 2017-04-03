About file-plugin.c

This plugin simply reads a file and writes its contents to
a buffer.

In a typical Traffic Server installation, this buffer is
written to traffic.out.

A similar function is used in the append-transform plugin
which reads in text from a file, and appends the text to the
bodies of html response documents. (See the load function in
append-transform.c).

To use this plugin, you would need a line like this in
plugin.config:

	file_1.so path/to/file.text

Enter either an absolute or a relative pathname for the file.
If you use a relative pathname, the path must be specified with
respect to the Traffic Server install directory.  (That is, the path
contained in /etc/traffic_server.)

The only function defined is TSPluginInit.

It does the following:

- opens the file specified in plugin.config, using
	TSfopen

- reads the content of the file into a buffer using
	TSfgets

- closes the file using
	TSfclose
