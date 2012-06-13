#!/usr/bin/python

# Author: opensource@navyaprabha.com
# Description: Sample script to add keys to memcached for use with YTS/memcached_remap plugin

import memcache

# connect to local server
mc = memcache.Client(['127.0.0.1:11211'], debug=0)

# Add couple of keys
mc.set("http://127.0.0.1:80/", "http://127.0.0.1:8080");
mc.set("http://localhost:80/", "http://localhost:8080");

# Print the keys that are saved
print "response-1 is '%s'" %(mc.get("http://127.0.0.1:80/"))
print "response-2 is '%s'" %(mc.get("http://localhost:80/"))

