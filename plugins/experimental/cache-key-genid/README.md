ats-plugin-cache-key-genid
==========================

Apache Traffic Server (ATS) plugin to modify the URL used as the cache key by adding a generation ID tag to the hostname.
This is useful when ATS is running in reverse proxy mode and proxies several (ie hundreds or thousands) of hosts.
Each host has a generation ID (genid) that's stored in a small embedded kytocabinet database.
Without this plugin, the CacheUrl is set to the requested URL.  

For example, if the requested url is  http://example.tld/foobar.css, then natively the CacheUrl is http://example.tld/foobar.css.
With this plugin, the CacheUrl is set to http://example.tld.#/foobar.css, where # is an integer representing example.tld's genid.

## License

Copyright &copy; 2013 Go Daddy Operating Company, LLC 

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.


## Why use this plugin?

The simple answer:  fast clear cache operation.  

By incrementing a hosts genid, you instantly invalidate all of that host's files.  Not really invalidate, but make it impossible to find.

When an HTTP request comes into ATS, it takes the URL and generates the CacheUrl, then looks in the cache for the key matching md5(CacheUrl).
If CacheUrl changes, the md5 hash changes, and it won't find old copies.

This method is exceptionally faster than http://localhost/delete_regex=http://example\.tld/.*.  ATS's delex_regex method performs a full cache scan. 
It looks at every file in the cache, determines if it matches the regular expresssion, and then deletes the file.  Using ats-plugin-cache-key-genid, no cache
scan is required.  You simply increment a value in a kytocabinet database and done.

## The high level workflow of what this plugin does

The solution is to not actually delete the files, but instead increment a counter in an embedded database local to each ATS server. 
ats-plugin-cache-key-genid modifies the cache-key to include the host's genid.

* Intercept incoming http requests
* Hook just before the cache key is set
* The cache key is effectively md5(url) or md5(http://host/path). Change it to md5(http://host.genid/path)
	* Take the url
	* Find the host
	* Lookup the host's genid in an embeded, super fast, super lightweight, mostly in memory, key/value pair kyotocabinet database
	* Make a newurl string by injecting the host's genid just after the host in the original url. ie http://foo.com/style.css becomes http://foo.com.2/style.css
* Call TSCacheUrlSet with the newurl

How do you accomplish the genid increment?  Below we give you the kytocabinet command to do so.  Presumably, you have some form of user interface where users request 
a Clear Cache operation.  You must relay this to your ATS server(s) and command them to increment that host's genid.  There are many designs available for this.
You either push or pull the command.  This might be accomplished by an event bus, for example.  Passing these requests to the ATS servers is beyond 
the scope of this write up.

## Requires

* Apache Traffic Server (http://trafficserver.apache.org/)
	* Note: we used ATS 3.0 to build this, it may work with other versions.
* Kyto Cabinet (http://fallabs.com/kyotocabinet/)
	* Note: we used kyotocabinet-1.2.76 to build ours, it may work with other versions.

The instructions below assume you have Apache Traffic Server installed in /opt/ats and Kyoto Cabinet installed in /opt/kyotocabinet.  If your installs are in different directories, change the paths in the following commands accordingly.

## to compile
```bash
/opt/ats/bin/tsxs -o cache-key-genid.so -c cache-key-genid.c
```

## to compile in libkyotocabinet
```bash
gcc -shared -Wl,-E -o cache-key-genid.so cache-key-genid.lo /opt/kyotocabinet/lib/libkyotocabinet.a
```

## to put into libexec/trafficserver/
```bash
sudo /opt/ats/bin/tsxs -o cache-key-genid.so -i
```

## to create the kyotocabinet database
```bash
sudo /opt/kyotocabinet/bin/kcpolymgr create -otr /opt/ats/var/trafficserver/genid.kch
# replace "ats:disk" with the user:group that runs your ATS server
sudo chown ats:disk /opt/ats/var/trafficserver/genid.kch
```

## to add/modify a record in the kyotocabinet database
```bash
sudo /opt/kyotocabinet/bin/kcpolymgr set -onl /opt/ats/var/trafficserver/genid.kch example.tld 5
```

## to get a record from the kyotocabinet database
```bash
/opt/kyotocabinet/bin/kcpolymgr get -onl /opt/ats/var/trafficserver/genid.kch example.tld 2>/dev/null
```

## Set ATS debug to ON in records.config like this (do not do this in production):
```bash
CONFIG proxy.config.diags.debug.enabled INT 1
CONFIG proxy.config.diags.debug.tags STRING cache-key-genid
```

If you turn the debug on like this, then you can tail the traffic.out file and witness the discovery of the url and the CacheUrl transformation.
You would not want to run this in production, b/c you'd be writing too much to the log file, which would slow down ATS.
It's great for dev/test/debug, however, so you know it's working well.


