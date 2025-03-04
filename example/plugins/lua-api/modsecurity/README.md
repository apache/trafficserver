Integrating ATS with ModSecurity V3 using LuaJIT and FFI
====

Open source WAF for ATS

Tested with the following
====
 - ModSecurity v3.0.13
 - ATS 10.0.2

How to Install the Example
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
 - return any request with query parameter of `testparam=test1` with 301 redirect response to https://www.example.com/
 - override any response with header `test` equal to `1` with a 403 status response
 - override any response with header `test` equal to `2` with a 301 redirect response to https://www.example.com/
 - write debug log out to `/tmp/debug.log`

Working with CRS
====
 - Go [here](https://github.com/coreruleset/coreruleset) and download release v4.10.0
 - Uncompress the contents and copy `crs-setup.conf.example` to `/usr/local/var/modsecurity` and rename it to `crs-setup.conf`
 - Copy all files in `rules` directory to `/usr/local/var/modsecurity/rules`
 - Copy `owasp.conf` in this repository to `/usr/local/var/modsecurity`
 - Change `/usr/local/etc/trafficserver/plugin.config` to add the following line and restart ats

```
tslua.so --enable-reload /usr/local/var/lua/ats-luajit-modsecurity.lua /usr/local/var/modsecurity/owasp.conf
```

 - To test, run a request with "User-Agent: Nikto" header. And it should trigger the default action to log warning message to traffic.out

Extra Notes with CRS
====
 - Please check out this [link](https://github.com/SpiderLabs/ModSecurity/issues/1734) for performance related information
 - To turn on debugging, you can uncomment the following inside `owasp.conf`

```
SecDebugLog /tmp/debug.log
SecDebugLogLevel 9
```

- We can use `SecRuleRemoveById` inside `owasp.conf` to remove rules. E.g those checking for request and response body. This trick can be used to remove other rules that does not apply well in some situations


TODOs/Limitations
====
 - No support for `REQUEST_BODY` examination (We need to buffer the request body for examination first before we send to origin.)
 - No support for `RESPONSE_BODY` examination (We need to uncompress the contents first if they are gzipped. And that will be expensive operation for proxy). See https://github.com/SpiderLabs/ModSecurity/issues/2494 for reference
 - How does this work with the lua engine inside ModSecurity V3?
 - Unit Test using busted framework
 - More functional testing needed
 - Performance testing - impact to latency and capacity
