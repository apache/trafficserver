.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../common.defs

.. _getting-started:

Getting Started
***************

.. toctree::
   :maxdepth: 2

Introduction
============

|ATS| provides a high-performance and scalable software solution for both
forward and reverse proxying of HTTP/HTTPS traffic, and may be configured to
run in either or both modes simultaneously. This Getting Started guide explains
the basic steps an administrator new to |TS| will need to perform to get the
software up and running in a minimal configuration as quickly as possible.

Example Scenario
----------------

In this guide, we will use the fictional company |AW| as a basis for the
configuration examples. |AW| has a product brochure website (assumed to reside
at the domain ``www.acme.com``) that performs very poorly. The content management
software they chose takes an unbearable amount of time to generate pages on
every request and their engineering team has chosen |TS| as a caching proxy
layer to improve site performance.

Separately, |AW| has decided to use |TS| to help improve the performance of
their office's Internet access, which is hobbled by their reliance on an aging
leased line and certain employees' predilection for extracurricular web
browsing.

Terminology
-----------

This guide uses some terms which may be unfamiliar to administrators new to
proxy servers.

Origin Server
    The server which generates the content you wish to proxy (and optionally
    cache) with |TS|. In a forward proxy configuration, the origin server may be
    any remote server to which a proxied client attempts to connect. In a
    reverse proxy configuration, the origin servers are typically a known set of
    servers for which you are using |TS| as a performance-enhancing caching
    layer.

Reverse Proxy
    A reverse proxy appears to outside users as if it were the origin server,
    though it does not generate the content itself. Instead, it intercepts the
    requests and, based on the configured rules and contents of its cache, will
    either serve a cached copy of the requested content itself, or forward the
    request to the origin server, possibly caching the content returned for use
    with future requests.

Forward Proxy
    A forward proxy brokers access to external resources, intercepting all
    matching outbound traffic from a network. Forward proxies may be used to
    speed up external access for locations with slow connections (by caching the
    external resources and using those cached copies to service requests
    directly in the future), or may be used to restrict or monitor external
    access.

Transparent Proxy
    A transparent proxy may be either a reverse or forward proxy (though nearly
    all reverse proxies are deployed transparently), the defining feature being
    the use of network routing to send requests through the proxy without any
    need for clients to configure themselves to do so, and often without the
    ability for those clients to bypass the proxy.

For a more complete list of definitions, please see the :ref:`glossary`.

Installation
============

As with many other software packages, you may have the option of installing
|ATS| from your operating system distribution's packages, or compiling and
installing from the source code yourself. Distribution packages may lag behind
the current stable release of |TS|, sometimes by a significant amount. While
we will cover briefly the packages required for common operating system
distributions, the remainder of this guide will be assuming the latest stable
release of |TS| when discussing configuration parameters and available features.

.. _gs-install-from-package:

Installing From Distribution Packages
-------------------------------------

It is recommended that you install from source code, as described in the section
below, rather than rely on your distribution's packages if you wish to have
access to the latest features and fixes in |TS|.

Ubuntu
~~~~~~

The Canonical repositories up through, and including, Utopic Unicorn only
provide |TS| v3.2.x. ::

    sudo apt-get install trafficserver

RHEL / CentOS
~~~~~~~~~~~~~

|TS| is available through the EPEL repositories. If you do not have those
configured on your machine yet, you must install them first with the following::

    wget https://dl.fedoraproject.org/pub/epel/epel-release-latest-7.noarch.rpm
    sudo rpm -Uvh epel-release-7*.rpm

Ensuring that you replace the release number with a value that is appropriate
for your system. Once you have EPEL installed, you may install |TS| itself. ::

    sudo yum install trafficserver

OmniOS (illumos)
~~~~~~~~~~~~~~~~

OmniOS (an illumos-based distribution) provides the |TS| package in its public
*OmniTI-MS* publisher. ::

    sudo pkg set-publisher -g http://pkg.omniti.com/omniti-ms/ ms.omniti.com
    sudo pkg install omniti/server/trafficserver

The latest release published, at the time of this writing, is |TS| v4.2.1.

.. _gs-install-from-source:

Installing From Source Code
---------------------------

To install from source code, you will need to have the following tools and
libraries on the machine used to build |TS|:

- pkgconfig
- libtool
- gcc (>= 4.3 or clang > 3.0)
- GNU make
- openssl
- pcre
- libcap
- flex (for TPROXY)
- hwloc
- lua
- zlib
- curses (for traffic_top)
- curl (for traffic_top)

To build |TS| from a Git clone (the method we will be using in this guide), you
will also need the following:

- git
- autoconf
- automake

In this guide, |TS| will be built to use the default ``nobody`` user and group
and will be installed to ``/opt/ts``. It is assumed that all of the dependencies
are located in standard paths. If this is not the case, you may need to use the
appropriate ``--with-<package>`` options as detailed in the output of
``./configure --help``.

#. Clone the repository (you may skip this if you have downloaded an archive of
   the source code to build a specific official release of |TS| instead of the
   HEAD from source control)::

    git clone https://git-wip-us.apache.org/repos/asf/trafficserver.git

#. Change to the cloned (or unarchived) directory::

    cd trafficserver/

#. If you have cloned the repository from Git, you will need to generate the
   ``configure`` script before proceeding::

    autoreconf -if

#. Configure the source tree::

    ./configure --prefix=/opt/ts

#. Build |TS| with the generated Makefiles, and test the results::

    make
    make check

#. Install |TS| to the configured location::

    sudo make install

#. Optionally, you may find it prudent to run the regression tests on your newly
   installed |TS|::

    cd /opt/ts
    sudo bin/traffic_server -R 1

Configuration
=============

We will be tackling two separate configuration scenarios in the following
sections. The first is the most common application of a performance-enhancing
caching proxy for externally-facing websites, a transparent and caching reverse
proxy which forwards all requests presented to it to a single origin address
and caches the responses based on their cache control headers (as well as some
simple heuristics for specific content types when cache control headers are not
present).

The second configuration we will review is a very basic transparent forward
proxy, typically used in situations where you either need to improve the
performance of a local network's use of external resources, or you wish to have
the capability of monitoring or filtering the traffic.

Configuring A Reverse Proxy
---------------------------

A minimal reverse proxy configuration requires changes to only a few
configuration files, which will all be located in the
``/opt/ts/etc/trafficserver`` directory if you have configured your installation
per the instructions in :ref:`gs-install-from-source` above.

For these examples, we will be assuming that |TS| is running on the same host
machine as the origin website. This is not a requirement, and you may choose to
run |TS| on an entirely different host, even connected to entirely different
networks, as long as |TS| is able to reach the origin host.

Enable Reverse Proxying
~~~~~~~~~~~~~~~~~~~~~~~

Within the :file:`records.config` configuration file, ensure that the following
settings have been configured as shown below::

    CONFIG proxy.config.http.cache.http INT 1
    CONFIG proxy.config.reverse_proxy.enabled INT 1
    CONFIG proxy.config.url_remap.remap_required INT 1
    CONFIG proxy.config.url_remap.pristine_host_hdr INT 1
    CONFIG proxy.config.http.server_ports STRING 8080 8080:ipv6

:ts:cv:`proxy.config.http.cache.http`
    Enables caching of proxied HTTP requests.

:ts:cv:`proxy.config.reverse_proxy.enabled`
    Enables reverse proxying support.

:ts:cv:`proxy.config.url_remap.remap_required`
    This setting requires that a remap rule exist before |TS| will proxy the
    request and ensures that your proxy cannot be used to access the content of
    arbitrary websites (allowing someone of malicious intent to potentially
    mask their identity to an unknowing third party).

:ts:cv:`proxy.config.url_remap.pristine_host_hdr`
    This setting causes |TS| to keep the ``Host:`` client request header intact
    which is necessary in cases where your origin servers may be performing
    domain-based virtual hosting, or taking other actions dependent upon the
    contents of that header.

:ts:cv:`proxy.config.http.server_ports`
    This configures |TS| to bind itself to the port ``8080`` for HTTP traffic,
    for both IPv4 and IPv6.

Configure Origin Location
~~~~~~~~~~~~~~~~~~~~~~~~~

The previous settings enable reverse proxying (and prevent flagrant abuse of
it), but now |TS| needs to know what to proxy. This is achieved by writing
remap rules, which make use of the core :ref:`admin-plugins-conf-remap`. For
our Getting Started guide's |AW| example scenario, we have very simple needs
and want little more than to proxy all requests to our single origin server.
This is accomplished with the following rule added to the :file:`remap.config`
configuration::

    map http://www.acme.com/ http://localhost:80/

With this mapping rule, all paths that |TS| receives with a Host: header of
``www.acme.com`` will be proxied to ``localhost:80``. For instance, a request
for ``http://www.acme.com/foo/bar`` will be proxied to ``http://localhost:80/foo/bar``,
while requests with other Host: headers will be rejected.

It is worth pausing at this point to note that in a reverse proxying scenario,
it is |TS| itself which should be responding to HTTP requests made to your
public domain. While you first configure and evaluate whether |TS| will meet
your needs, your origin server will continue to reside at the public-facing
domain name on the default ports, should you move your |TS| configuration into
production your DNS records for the domain(s) you wish to proxy and cache should
resolve to the host(s) running |TS| (in the event that you run it on a separate
host from your origin). Your origin should be accessible at a different address
(or bind to different ports if you are running both your origin service and |TS|
on the same host) and should no longer receive requests sent to the primary
domain on its default ports.

In our |AW| scenario, they ultimately decide to deploy |TS| at which point,
since they are running both |TS| and their origin web server on the same host,
they reconfigure their origin service to listen on port ``8080`` instead of the
default, and change |TS| to bind to ``80`` itself. Updating the remap is thus
required, and it should now be::

    map http://www.acme.com/ http://localhost:8080/

Now all requests made to ``www.acme.com`` are received by |TS| which knows to
proxy those requests to ``localhost:8080`` if it cannot already serve them from
its cache. Because we enabled pristine host headers earlier, the origin service
will continue to receive ``Host: www.acme.com`` in the HTTP request.

If |AW| decides to use |TS| to reverse proxy a second domain ``static.acme.com``
with a different origin server than the original, they need to make further
changes, as a new remap line needs to be added to handle the additional domain::

    map http://static.acme.com/ http://origin-static.acme.com/

If they also decide to have requests to ``www.acme.com`` with paths that start with
``/api`` to a different origin server. The api origin server shouldn't get the ``/api``,
they will remap it away. And, since the above remap rules catch all paths,
this remap rule needs to be above it::

    map http://www.acme.com/api/ http://api-origin.acme.com/

With this remap rule in place, a request to ``http://www.acme.com/api/example/foo``
will be proxied to ``http://api-origin.acme.com/example/foo``.

Finally, if |AW| decides to secure their site with https, they will need two
additional remap rules to handle the https requests. |TS| can translate an inbound
https request to an http request to origin. So, they would have additional remap
rules like::

    map https://www.acme.com/ http://localhost:8080/
    map https://static.acme.com/ https://origin-static.acme.com/

This will require installing a certificate, and adding a line to
:file:`ssl_multicert.config`. Assuming the cert has the static.acme.com alternate
name, and that cert should be presented by default::

    dest_ip=* ssl_cert_name=/path/to/secret/privatekey/acme.rsa

Further information about configuring |TS| for TLS can be found :ref:`admin-ssl-termination`
section of the documentation.

Adjust Cache Parameters
~~~~~~~~~~~~~~~~~~~~~~~

The default |TS| configuration will provide a 256 MB disk cache, located in
``var/trafficserver/`` underneath your install prefix. You may wish to adjust
either or both of the size and location of this cache. This is done with the
:file:`storage.config` configuration file. In our example, |AW| has dedicated
a large storage pool on their cache server which is mounted at ``/cache``. To
use this, and to disable the default cache storage setting, the following will
be the sole entry in :file:`storage.config`::

    /cache/trafficserver 500G

.. note:: Changes to the cache configuration require a restart of |TS|.

You may also wish to use raw devices, or partition your cache storage. Details
on these features may be found in the :ref:`admin-configuration` section of
the Administrator's Guide.

Final Configurations
~~~~~~~~~~~~~~~~~~~~

Once completed, the following configuration files for |AW| contain the following
entries:

:file:`records.config`::

    CONFIG proxy.config.http.cache.http INT 1
    CONFIG proxy.config.reverse_proxy.enabled INT 1
    CONFIG proxy.config.url_remap.remap_required INT 1
    CONFIG proxy.config.url_remap.pristine_host_hdr INT 1
    CONFIG proxy.config.http.server_ports STRING 80 80:ipv6

:file:`remap.config`::

    map http://www.acme.com/api/ http://api-origin.acme.com/
    map https://www.acme.com/api/ https://api-origin.acme.com/
    map http://www.acme.com/ http://localhost:8080/
    map https://www.acme.com/ http://localhost:8080/
    map http://static.acme.com/ http://origin-static.acme.com/
    map https://static.acme.com/ https://origin-static.acme.com/

:file:`storage.config`::

    /cache/trafficserver 500G

:file:`ssl_multicert.config`::

    ssl_cert_name=/path/to/secret/acme.rsa

Configuring A Forward Proxy
---------------------------

Configuring a forward proxy with |TS| is somewhat more straightforward, though
there are two main approaches for how to direct client traffic through the
proxy: explicit or transparent.

More detail on the process is also available in the Administrator's Guide in
the :ref:`forward-proxy` section. This guide will cover only a minimal
configuration.

Enable Forward Proxying
~~~~~~~~~~~~~~~~~~~~~~~

Contrary to a reverse proxy, where you have a defined list of origin servers for
which you wish to proxy (and optionally cache), a forward proxy is used to
proxy (and optionally cache) for arbitrary remote hosts. As such, the following
settings in :file:`records.config` are the base configuration for a minimal
forward proxy::

    CONFIG proxy.config.url_remap.remap_required INT 0
    CONFIG proxy.config.http.cache.http INT 1

:ts:cv:`proxy.config.url_remap.remap_required`
    Disables the requirement for a remap rule to exist and match the incoming
    request before |TS| will proxy the request to the remote host.

:ts:cv:`proxy.config.http.cache.http`
    Enables caching of proxied HTTP requests.

If your installation will be strictly a forwarding proxy, then reverse proxying
should be explicitly disabled::

    CONFIG proxy.config.reverse_proxy.enabled INT 0

Explicit Versus Transparent
~~~~~~~~~~~~~~~~~~~~~~~~~~~

With forward proxying enabled, the next step is deciding how clients will
connect through the proxy server. Explicit forward proxying requires that every
client application be configured (manually or through whatever configuration
management system you may employ) to use the proxy. The presence of the proxy
will be known to clients, and any clients not configured to use the proxy will
bypass it entirely unless you otherwise block network access for unproxied
traffic.

With transparent proxying, clients require no configuration and may be given no
option to bypass the proxy. This configuration requires your network to route
all requests automatically to the proxy. The details of how to accomplish this
routing are entirely dependent upon the layout of your network and the routing
devices you use.

For a more detailed discussion of the options, and starting points for
configuring each in your network environment, please refer to the
:ref:`forward-proxy` section of the Administrator's Guide.

Logging and Monitoring
======================

Configuring Log Output
----------------------

The log formats used by |TS| are highly configurable. While this guide will not
go into full detail of this versatility, it is useful to consider what style of
logging you would like to perform. If your organization already makes use of
log monitoring or analysis tools that understand, for example, *Netscape
Extended-2* format you may wish to enable that logging format in addition to,
or instead of, the default |TS| logs.

The Administrator's Guide discusses logging options in great detail in
:ref:`admin-logging`.

Further Steps
=============

By this point, you should have a fully functioning caching proxy, whether to
help improve the customer facing performance of your website, or to assist in
speeding up Internet access for your office while allowing for the possibility
of access control, content filtering, and/or usage monitoring. However, it is
quite likely that your installation is not yet tuned properly and may not be
providing the best experience |ATS| has to offer. It is strongly recommended
that you consult the :ref:`performance-tuning` guide.

You may also want to learn more about :ref:`admin-monitoring`, or ensuring
that your installation is properly secured by reading the :ref:`admin-security`
section. Properly sizing your cache, both the on-disk cache and the companion
memory cache, are important topics covered in :ref:`admin-configuration`.

