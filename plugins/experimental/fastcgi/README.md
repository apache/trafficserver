# AtsFastcgi -- a FastCGI plugin implementation for Apache Traffic Server.

This plugin was presented at the ATS Euro-Tour summit in Cork [1]. Its current state
should be considered alpha, the plugin has seen 0 production mileage.

[1] Slides: https://docs.google.com/presentation/d/1kJ3hxbmflh8z4jsquPskeb1xNrLXHAYI2-OQXwwftZU/edit

## Install

```bash
cd src

ATS_SRC="/home/oschaaf/trafficserver"
ATS_EXEC="/usr/local"
make ATS_SRC="$ATS_SRC" ATS_EXEC="$ATS_EXEC"
sudo make ATS_SRC="$ATS_SRC" ATS_EXEC="$ATS_EXEC" install
```


## CONFIG

Add to plugin.config:
```
# For possible settings, see below
ats_mod_fcgi.so /usr/local/etc/trafficserver/fcgi.config
```

## Starting CGI

```
sudo apt-get install php-cgi
php-cgi -b 60000
```

## Running ATS

```
sudo traffic_server -T .*cgi.*
```

## Requesting a php page


```
curl 127.0.0.1/test.php
```

## Config php:

Default directory where php looks for scripts is "/var/www/html/
Simple test page that dump php info:

```
oschaaf@ats-fastcgi:/var/www/html⟫ cat test.php

<?php
// Show all information, defaults to INFO_ALL
phpinfo();
// Show just the module information.
// phpinfo(8) yields identical results.
phpinfo(INFO_MODULES);
?>
```

## Settings

```
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.enabled INT 1
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.hostname STRING localhost
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.server_ip  STRING 127.0.0.1
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.server_port STRING 60000
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.include STRING etc/trafficserver/fastcgi.config
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.document_root STRING /var/www/
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.html STRING index.php
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.min_connections INT 2
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.max_connections INT 16
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.max_requests INT 1000
ats_mod_fcgi.config:CONFIG proxy.config.http.fcgi.host.request_queue_size INT 250
```

### Old/stale docs: https://github.com/We-Amp/AtsFastcgi/wiki/Building-ATS-FCGI-from-Source
