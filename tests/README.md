
# Getting Started

This directory contains different tests for Apache Trafficserver. It is recommended that all tests move to this common area under the correct location based on the type of test being added.

## Layout
The current layout is:

**gold_tests/** - contains all the TSQA v4 based tests that run on the Reusable Gold Testing System (AuTest)

**tools/** - contains programs used to help with testing.

**include/** - contains headers used for unit testing.

## Scripts

To help with easy running of the tests, there is autest.sh and bootstrap.py.

### autest.sh
This file is a simple wrapper that will call the Reusable Gold Testing System (Autest) program in a pipenv. If the pipenv is not setup, the script will prompt user the missing components. That will set up the Autest on most systems in a Python virtual environment. The wrapper adds some basic options to the command to point to the location of the tests. Use --help for more details on options for running Autest.

### test-env-check.sh
This script will check for the necessary packages needed to create a pipenv that can run Autest. If any package is missing, the script will alert the user. If all packages are available, it install a virtual environment using the provided Pipfile.

### Pipfile
This file is used to setup a virtual environment using pipenv. It contains information including the packages needed for Autest. 
A set of commands for pipenv:
 * **pipenv install**: create virtual environment from the Pipfile. 
 * **pipenv shell**: launch a shell with the environment running(type "exit" to leave the shell).
 * **pipenv run cmd**: run command in the virtual environment without entering a shell, where cmd is the shell command to run.
 * **pipenv --rm**: remove the environment.

# Basic setup

AuTest can be run using the script file autest.sh listed above. Run the file from the tests/ directory followed by --ats-bin and the bin name. (ie ~/ats/bin) This will run the wrapper for the tests.  

To run autest manually, the recommended way is to follow these steps:
1. **pipenv install**: create the virtual environment(only needed once).
2. **pipenv shell**: enter a shell in the virtual environment(type "exit" to leave the shell).
3. **cd gold_tests**: enter the directory containing the test files.
4. **autest --ats-bin user_ats_bin**: run autest where user_ats_bin is the bin directory in the user's ats directory.

# Advanced setup

AuTest and the relevant tools can be install manually instead of using the wrapper script. By doing this, it is often easier to debug issues with the testing system, or the tests. There are two ways this can be done.
1. Run the bootstrap script then source the path with a "source ./env-test/bin/activate" command. At this point autest command should run without the wrapper script
2. Make sure you install python 3.5 or better on your system. From there install these python packages ( ie pip install ):
  - hyper
  - git+https://bitbucket.org/autestsuite/reusable-gold-testing-system.git
  - [traffic-replay](https://bitbucket.org/autestsuite/trafficreplay/src/master/) (This will automatically install [MicroDNS](https://bitbucket.org/autestsuite/microdns/src/master/), [MicroServer](https://bitbucket.org/autestsuite/microserver/src/master/), [TrafficReplayLibrary](https://bitbucket.org/autestsuite/trafficreplaylibrary/src/master/), and dnslib as part of the dependencies.)

# Writing tests for AuTest
When writing for the AuTest system please refer to the current [Online Documentation](https://autestsuite.bitbucket.io/) for general use of the system. To use CurlHeader tester for testing output of curl, please refer to [CurlHeader README](gold_tests/autest-site/readme.md)

## Documentation of AuTest extension for ATS.
Autest allows for the creation of extensions to help specialize and simplify test writing for a given application domain. Minus API addition the extension code will check that python 3.5 or better is used. There is also a new command line argumented added specifically for Trafficserver:

--ats-bin < path to bin directory >

This command line argument will point to your build of ATS you want to test. At this time v6.0 or newer of Trafficserver should work.

### MakeATSProcess(name,command=[traffic_server],select_ports=[True])
 * name - A name for this instance of ATS
 * command - optional argument defining what process to use. Defaults to traffic_server.
 * select_ports - have the testing system auto select the ports to use for this instance of ATS

This function will define a sandbox for an instance of trafficserver to run under. The function will return a AuTest process object that will have a number of files and variables defined to make it easier for test definition.

#### Environment
The environment of the process will have a number of added environment variables to control trafficserver running the in the sandbox location correctly. This can be used to easily setup other commands that should run under same environment.

##### Example

```python
# Define default ATS
ts=Test.MakeATSProcess("ts")
# Call traffic_ctrl to set new generation
tr=Test.AddTestRun()
tr.Processes.Default.Command='traffic_ctl'
tr.Processes.Default.ReturnCode=0
# set the environment for traffic_control to run in to be the same as the "ts" ATS instance
tr.Processes.Default.Env=ts.Env
```

#### Variables
These are the current variables that are defined dynamically for Trafficserver

port - the ipv4 port to listen on
portv6 - the ipv6 port to listen on
admin_port - the admin port used. This is set even is select_port is False

#### File objects
A number of file objects are defined to help with adding values to a given configuration value to for a test, or testing a value exists in a log file. File that are defined currently are:

##### log files
 * squid.log
 * error.log
 * diags.log

##### config files
 * records.config
 * cache.config
 * congestion.config
 * hosting.config
 * ip_allow.config
 * logging.yaml
 * parent.config
 * plugin.config
 * remap.config
 * socks.config
 * splitdns.config
 * ssl_multicert.config
 * storage.config
 * volume.config

#### Examples

Create a server

```python
# don't set ports because a config file will set them
ts1 = Test.MakeATSProcess("ts1",select_ports=False)
ts1.Setup.ts.CopyConfig('config/records_8090.config','records.config')
```

Create a server and get the dynamic port value

```python
# Define default ATS
ts=Test.MakeATSProcess("ts")
#first test is a miss for default
tr=Test.AddTestRun()
# get port for command from Variables
tr.Processes.Default.Command='curl "http://127.0.0.1:{0}" --verbose'.format(ts.Variables.port)

```

Add value to a configuration file
```python
# setup some config file for this server
ts.Disk.records_config.update({
            'proxy.config.body_factory.enable_customizations': 3,  # enable domain specific body factory
            'proxy.config.http.cache.generation':-1, # Start with cache turned off
            'proxy.config.config_update_interval_ms':1,
        })
ts.Disk.plugin_config.AddLine('xdebug.so')
ts.Disk.remap_config.AddLines([
            'map /default/ http://127.0.0.1/ @plugin=generator.so',
            #line 2
            'map /generation1/ http://127.0.0.1/' +
            ' @plugin=conf_remap.so @pparam=proxy.config.http.cache.generation=1' +
            ' @plugin=generator.so',
            #line 3
            'map /generation2/ http://127.0.0.1/' +
            ' @plugin=conf_remap.so @pparam=proxy.config.http.cache.generation=2' +
            ' @plugin=generator.so'
        ])
```

### CopyConfig(file, targetname=None, process=None)
* file - name of the file to copy. Relative paths are relative from the test file location
* targetname - the name name of the file when copied to the correct configuration location
* process - optional process object to use for getting path location to copy to. Only needed if the Setup object call is not in the scope of the process object created with the MakeATSProcess(...) API.

This function copies a given configuration file the location of a given trafficserver sandbox used in a test. Given a test might have more than on trafficserver instance, it can be difficult to understand the correct location to copy to. This function will deal with the details correctly.

#### Examples

Copy a file over

```python
ts1 = Test.MakeATSProcess("ts1",select_ports=False)
# uses the setup object in the scope of the process object
ts1.Setup.ts.CopyConfig('config/records_8090.config','records.config')
```
```python
ts1 = Test.MakeATSProcess("ts1",select_ports=False)
# uses the Setup in the global process via a variable passing
Test.Setup.ts.CopyConfig('config/records_8090.config','records.config',ts1)
# same as above, but uses the dynamic object model form
Test.Setup.ts.CopyConfig('config/records_8090.config','records.config',Test.Processes.ts1)
```

## Setting up Origin Server
### Test.MakeOriginServer(Name)
 * name - A name for this instance of Origin Server.

 This function returns a AuTest process object that launches the python-based microserver. Micro-Server is a mock server which responds to client http requests. Microserver needs to be setup for the tests that require an origin server behind ATS. The server reads a JSON-formatted data file that contains request headers and the corresponding response headers. Microserver responds with payload if the response header contains Content-Length or Transfer-Enconding specified.

### addResponse(filename, request_header, response_header)
* filename - name of the file where the request header and response header will be written to in JSON format
* request_header - dictionary of request header
* response_header - dictionary of response header corresponding to the request header.

This function adds the request header and response header to a file which is then read by the microserver to populate request-response map. The key-fields required for the header dictionary are 'headers', 'timestamp' and 'body'.

### Example
```python
#create the origin server process
server=Test.MakeOriginServer("server")
#define the request header and the desired response header
request_header={"headers": "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
#desired response form the origin server
response_header={"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n", "timestamp": "1469733493.993", "body": ""}
#addResponse adds the transaction to a file which is used by the server
server.addResponse("sessionlog.json", request_header, response_header)
#add remap rule to traffic server
ts.Disk.remap_config.AddLine(
    'map http://www.example.com http://127.0.0.1:{0}'.format(server.Variables.Port)
)
```

## Setting up DNS
### Test.MakeDNServer(name, default=None)
 * name - A name for this instance of the DNS.
 * default - if a list argument is provided, uDNS will reply with the list contents instead of NXDOMAIN if a DNS can't be found for a partcular entry

 This function returns a AuTest process object that launches the python-based microDNS (uDNS). uDNS is a mock DNS which responds to DNS queries. uDNS needs to be setup for the tests that require made-up domains. The server reads a JSON-formatted data file that contains mappings of domain to IP addresses. uDNS responds with the approriate IP addresses if the requested domain is in uDNS' mappings.

 * addRecords(records=None, jsonFile=None)

 This function adds records using either a dictionary, *records*, or a json file, *jsonFile*.

 The supplied dictionary must be in the form of ```{ 'domain A': [IP1, IP2], 'domain B': [IP3, IP4] }```.

 The supplied json file must take the form of
 ```
 {
     "mappings": [
         {domain A: [IP1, IP2]},
         {domain B: [IP3, IP4]}
     ]
 }
 ```

 ### Examples
 There are 3 ways to utilize uDNS -

 Easy way if everything is done on localhost - by adding default option to Test.MakeDNServer:
 *uDNS by default returns NXDOMAIN for any unknown mappings*

 ```python
    # create TrafficServer and uDNS processes
    ts = Test.MakeATSProcess("ts")
    dns = Test.MakeDNServer("dns", default=['127.0.0.1'])

    ts.Disk.records_config.update({
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port), # let TrafficServer know where the DNS is located
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.url_remap.remap_required': 0  # need this so TrafficServer won't return error upon not finding the domain in its remap file
    })
 ```

 Using the *addRecords* method:
 ```python
    # create TrafficServer and uDNS processes
    ts = Test.MakeATSProcess("ts")
    dns = Test.MakeDNServer("dns")

    ts.Disk.records_config.update({
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port), # let TrafficServer know where the DNS is located
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.url_remap.remap_required': 0  # need this so TrafficServer won't return error upon not finding the domain in its remap file
    })

    dns.addRecords(records={"foo.com.":["127.0.0.1", "127.0.1.1"]})
    # AND/OR
    dns.addRecords(jsonFile="zone.json") # where zone.json is in the format described above
 ```

 Without disabling remap_required:
 ```python
    # create TrafficServer and uDNS processes
    ts = Test.MakeATSProcess("ts")
    dns = Test.MakeDNServer("dns")

    ts.Disk.records_config.update({
        'proxy.config.dns.resolv_conf': 'NULL',
        'proxy.config.dns.nameservers': '127.0.0.1:{0}'.format(dns.Variables.Port) # let TrafficServer know where the DNS is located
    })

    # if we don't disable remap_required, we can also just remap a domain to a domain recognized by DNS
    ts.Disk.remap_config.AddLine(
        'map http://example.com http://foo.com'
    )

    dns.addRecords(records={"foo.com.":["127.0.0.1", "127.0.1.1"]})
 ```

## Condition Testing
### Condition.HasATSFeature(feature)
 * feature - The feature to test for

 This function tests for Traffic server for possible feature it has been compiled with. Current Features you can test for are:
 * TS_HAS_LIBZ
 * TS_HAS_LZMA
 * TS_HAS_JEMALLOC
 * TS_HAS_TCMALLOC
 * TS_HAS_IN6_IS_ADDR_UNSPECIFIED
 * TS_HAS_BACKTRACE
 * TS_HAS_PROFILER
 * TS_USE_FAST_SDK
 * TS_USE_DIAGS
 * TS_USE_EPOLL
 * TS_USE_KQUEUE
 * TS_USE_PORT
 * TS_USE_POSIX_CAP
 * TS_USE_TPROXY
 * TS_HAS_SO_MARK
 * TS_HAS_IP_TOS
 * TS_USE_HWLOC
 * TS_USE_SET_RBIO
 * TS_USE_LINUX_NATIVE_AIO
 * TS_HAS_SO_PEERCRED
 * TS_USE_REMOTE_UNWINDING
 * TS_HAS_128BIT_CAS
 * TS_HAS_TESTS
 * TS_HAS_WCCP
 * SPLIT_DNS

### Example
```python
#create the origin server process
Test.SkipUnless(
    Condition.HasATSFeature('TS_USE_LINUX_NATIVE_AIO'),
)
```

### Condition.HasCurlFeature(feature)
 * feature - The feature to test for

 This function tests for Curl for possible feature it has been compiled with. Consult Curl documenation for feature set.

### Example
```python
#create the origin server process
Test.SkipUnless(
    Condition.HasCurlFeature('http2'),
)
```

### Condition.PluginExists(pluginname)
 * pluginname - The plugin to test for

 This function tests for existence of a certain plugin in TrafficServer.

### Example
```python
Test.SkipUnless(
    Condition.PluginExists('a-plugin.so'),
)
```
