
	Thread pool sample plugin for SDK 3.0
	-------------------------------------

List of files:
==============
 Plugin
 ------
 - psi.c : proxy side include plugin
 - thread.h : thread pool header
 - thread.c : thread pool implementation

 Tools for testing
 -----------------
 - include/gen.c : utility to generate text files of various size
 - include/gen_inc.sh : script to generate include files for testing

 - test/SDKTest/psi_server.c : SDKTest server plugins to test psi
 - test/SDKTest/SDKtest_server.config : SDKTest config file


Description
===========
 The plugin looks for the specific header "X-Psi" in the OS HTTP response.
 If this header is found, and if the document is of type text/html,
 the plugin parses the response body to find include tags: "<--include=filename-->".

 When an include tag is found, the plugin substitutes it by the content
 of the file `filename`.

 Filename must be a simple filename. It can not contain a path (relative or absolute).

 Example:
 --------
 The OS sends the following HTTP response:
	HTTP/1.0 200 OK\r\n
	Content-Type: text/html\r\n
	X-Psi: true\r\n
	\r\n\r\n
	<HTML>
	My html source code
	<!--include=my_include.txt-->
	Some more html code
	</HTML>

 The text file 'include.txt' on the proxy filesystem contains:
	include content line 1
	include content line 2
	include content line 3

 The response processed by the proxy and finally sent to the client is:
	HTTP/1.0 200 OK\r\n
	Content-Type: text/html\r\n
	X-Psi: true\r\n
	\r\n\r\n
	<HTML>
	My html source code
	include content line 1
	include content line 2
	include content line 3
	Some more html code
	</HTML>
 Note that the content of the 'include.txt' file is now inserted into
 the body of the HTML response.


Architecture
============

 Pool of threads
 ---------------
 The psi plugin uses a pool of threads to make system blocking calls (access to disk).
 This is to avoid blocking a TS thread and slowing down the whole TS.

 High level algorithm:
 --------------------
 0. At init time, the plugin spawns threads.
 1. The plugin scans any HTTP response message header for the specific header X-Psi.
 2. If found, it sets up a transformation.
 3. The transformation parses the HTML content.
 4. When an include tag is found, the plugin creates a job and put it into
    the thread's queue of jobs.
 5. Then it goes into an inactive mode, waiting for the job to get done by a thread.
 6. A thread picks up the job. Reads the file from the disk (blocking system call).
    Then reenables the transformation.
 7. The transformation insert the content of the file into the HTML body.
 8. Go to step 8.



Plugin Installation
===================
 Add a line to Traffic Server plugin.config with the name of the plugin: psi.so
 No arguments required.

 The files to include must be located under the directory <plugin_path>/include
 <plugin_path> is the path where the psi.so library is located
 (by default $TS_HOME/etc/trafficserver/plugins)

 Start TS as usual. Now any HTTP response with the X-Psi headers will be processed
 by the PSI plugin.



Plugin Testing
==============

 Sample include file generation
 ------------------------------
 A basic utility ('gen')to generate files to be inserted is provided
 in the directory thread_pool/include.
 This create text files of various sizes.
 Compile gen the execute gen_inc.sh to generate files:
 > cd include
 > make
 > gen_inc.sh

 Load
 ----
 SDKTest can be used to test this plugin. SDKTest allows to simulate:
  - synthetic clients sending requests
  - a synthetic origin server

 The synthetic origin server has to be customized in order to send back
 responses that contains the specific 'X-Psi' header.
 This is done through a SDKTest server plugin.

 The rate of responses with X-Psi header is configurable thru a SDKTest config file.

 A SDKTest server plugin as well as a SDKTest configuration file
 are provided in the directory thread_pool/test/SDKTest.
 Refer to the SDKTest manual for detailed setup instructions.
