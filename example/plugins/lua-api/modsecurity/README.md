Integrating ATS with ModSecurity V3 using LuaJIT and FFI
====

Opensource WAF for [Apache Traffic Server](http://trafficserver.apache.org/).

Requirement
====
 - ModSecurity v3.0.4
 - ATS 8.0.8

How to Use
====
 - Copy all lua files to `/usr/local/var/lua`
 - Put the example modsecurity rule file (`example.conf`) to `/usr/local/var/modsecurity` , readable by the ATS process
 - Add a line in `/usr/local/etc/trafficserver/plugin.config` and restart ats

```
tslua.so --enable-reload /usr/local/var/lua/ats-luajit-modsecurity.lua /usr/local/var/modsecurity/example.conf
```

 - Changes can be made to example.conf and can be reloaded without restarting ATS. Just follow instructions [here](https://docs.trafficserver.apache.org/en/latest/appendices/command-line/traffic_ctl.en.html#cmdoption-traffic-ctl-config-arg-reload)

Contents/Rules inside example.conf
====
 - deny any request with query parameter of `testparam=test2` with a 403 status response
 - return any request with query parameter of `testparam=test1` with 301 redirect response to https://www.yahoo.com/
 - override any response with header `test` equal to `1` with a 403 status response
 - override any response with header `test` equal to `2` with a 301 redirect response to https://www.yahoo.com/
 - write debug log out to `/tmp/debug.log`

Working with CRS
====
 - Go to [here](https://github.com/SpiderLabs/owasp-modsecurity-crs) and get release v3.2.0
 - Uncompress the contents and copy `crs-setup.conf.example` to `/usr/local/var/modsecurity` and rename it to `crs-setup.conf`
 - Copy all files in `rules` directory to `/usr/local/var/modsecurity/rules`
 - Copy `owasp.conf` in this repository to `/usr/local/var/modsecurity`
 - Change `/usr/local/etc/trafficserver/plugin.config` to add the following line and restart ats

```
tslua.so --enable-reload /usr/local/var/lua/ats-luajit-modsecurity.lua /usr/local/var/modsecurity/owasp.conf
```

 - The following example curl command against your server should get a status 403 Forbidden response

```
curl -v -H "User-Agent: Nikto" 'http://<your server>/'
```

Extra Notes with CRS
====
 - Please check out this [link](https://github.com/SpiderLabs/ModSecurity/issues/1734) for performance related information
 - To turn on debugging, you can uncomment the following inside `owasp.conf`

```
SecDebugLog /tmp/debug.log
SecDebugLogLevel 9
```

- Rule ID 910100 in REQUEST-910-IP-REPUTATION.conf in `rules` directory requires GeoIP and have to be commented out if you do not built the modsecurity library with it.
- We use `SecRuleRemoveById` inside `owasp.conf` to remove rules checking for request and response body. This trick can be used to remove other rules that does not apply well in some situations


TODOs/Limitations
====
 - No support for `REQUEST_BODY` examination (We need to buffer the request body for examination first before we send to origin.)
 - No support for `RESPONSE BODY` examination (We need to uncompress the contents first if they are gzipped. And that will be expensive operation for proxy)
 - How does this work with the lua engine inside ModSecurity V3?
 - Unit Test using busted framework
 - More functional testing needed
 - Performance testing - impact to latency and capacity
