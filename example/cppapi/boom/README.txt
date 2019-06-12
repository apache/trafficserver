Boom is example of how to use a global plugin to attach transaction plugins to some of the transactions.

Boom is used to serve a custom page whenever the server responds with anything other than 200.


Usage:

To use Boom, put the plugin into the plugins directory and add the following line to plugins.config file:


/path/to/boom.so base_directory status_list


base_directory_path points to folder that contains custom error pages. The error pages names correspond to the errors, in the following ways:

NNN.html - error page that is specific to error NNN, e.g. "404.html" is specific to HTTP 404

Nxx.html - error page that is generic to error family Nxx, e.g. "4xx.html" can be served for HTTP 401, 402, 403 and so on

default.html - error page that is the most general fallback.


Custom page resolution order

Boom proceeds from the most specific to the most generic custom error page. The resolution comprises 5 phases:

Phase 1 - exact match is attempted
Phase 2 - status family match is attempted
Phase 3 - boom attempts to serve default.html file
Phase 4 - boom serves a compiled in fallback message.

To illustrate the resolution, lets assume the following scenario:

- plugins.config contains the line

/path/boom.so /path/errors 404,4xx,5xx

- /path/errors contains files

  /path/errors/404.html
  /path/errors/4xx.html
  /path/errors/3xx.html
  /path/errors/5xx.html

Lets consider the following scenarios:

HTTP 404

Phase 1 - exact match is attempted:
Since status code 404 is specified in the status_list, AND /path/errors/404.html is present, boom will serve /path/errors/404.html

HTTP 403

Phase 1 - exact match is attempted:
Since status 403 is not specified in the error list, boom skips to error family match

Phase 2 - error family match is attempted:
Since status wildcard 4xx is specified in the error list AND /path/errors/4xx.html is present, boom will serve /path/errors/4xx.html


HTTP 500
Phase 1 - exact match is attempted:
Since status 500 is not specified in the errors_list, boom skips to error family match

Phase 2 - error family match is attempted:
File /path/errors/5xx.html is present, however status wildcard 5xx is NOT specified. Therefore boom skips to general fallback match.

Phase 3 - general fallback match is attempted:
Since file /path/errors/default.html is not present, boom skips to the compiled in fallback

Phase 4 - compiled in fallback.
boom responds with the compiled in message "<html><body><h1>Your network will be back soon</h1></body></html>"
