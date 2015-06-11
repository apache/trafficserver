![ScreenShot](http://www.atsspeed.com/images/xATSSPEED_logo_plusshout_728x91.png.pagespeed.ic.8mRpu2PXS0.png
)

Apache Traffic Server web content optimization plugin powered by Google PageSpeed

http://www.atspagespeed.com/

To build, a simple 'make' should work. Use 'sudo make install' to install.
Optionally, patching ATS with ethread.patch helps with eliminating latency that 
sometimes gets induced when synchronising ATS's and PSOL's thread pools.

After that, update ATS's plugin.config with:
```
ats_pagespeed.so                                                                                 
gzip.so /usr/local/etc/trafficserver/gzip.config  
````
gzip.so also is build with ats_pagespeed, as it currently is a slightly
modified version of the official one from the ATS repository.

There are some hard-coded things in the plugin, these directories should exist:
- /tmp/ps_log/ to exist
- /tmp/ats_ps/ to exist

Configuration files go into `/usr/local/etc/trafficserver/psol.`
That folder is monitored, and changes to files in there are picked
up immediately. A sample configuration:

```
# Base configuration for the module, all host-specific configuration
# will inherit. 
pagespeed RewriteLevel CoreFilters
# Mandatory FileCachePath setting. The path must exist and be read/write for the traffic_server process.
pagespeed FileCachePath /tmp/ats_pagespeed/
# [host]
[192.168.185.185]
# Force traffic server to cache all origin responses
override_expiry
pagespeed FlushHtml on
pagespeed RewriteLevel CoreFilters
pagespeed EnableFilters rewrite_domains,trim_urls
pagespeed MapRewriteDomain http://192.168.185.185 http://www.foo.com
pagespeed MapOriginDomain http://192.168.185.185 http://www.foo.com
pagespeed EnableFilters prioritize_critical_css,move_css_to_head,move_css_above_scripts
pagespeed EnableFilters fallback_rewrite_css_urls,insert_img_dimensions,lazyload_images,local_storage_cache
pagespeed EnableFilters prioritize_critical_css,rewrite_css
pagespeed EnableFilters combine_javascript,combine_css
```

You can view debug output of the plugin using `traffic_server -T ".*speed.*"`

The current state compiles against PSOL 1.9.32.3-stable


