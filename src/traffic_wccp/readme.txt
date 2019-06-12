Wccp_client is a front end to the wccp client library.  It is a stand
alone program that speaks the client side of the WCCP cache protocol.

It can be used instead of the built in WCCP feature in Apache traffic server.
This can be beneficial if you have multiple programs running on the same
computer that are relying on WCCP to redirect traffic from the router to
the computer.

Since it relies on the wccp library, the wccp_client is only build if apache
traffic server is configured with --enable-wccp.

The overall Apache Traffic Server WCCP configuration documentation is
at https://docs.trafficserver.apache.org/en/latest/admin-guide/configuration/transparent-proxy.en.html

The wccp-client takes the following arguments.
--address IP address to bind.
--router Bootstrap IP address for routers.
--service Path to service group definitions.
--debug Print debugging information.
--daemon Run as daemon.

You need to run at least with the --address and the --service arguments. The
address should be an address assigned to one of your computer's interfaces.
An example service definition file, service-nogre-example.yaml, is included
in this directory.  In this file you define your MD5 security password
(highly recommended), and you define your service groups.  For each service
group you define how the service should be recognized (protocol and port),
the routers you are communicating with, whether you are using GRE or basic L2
routing to redirect packets.

In addition, you can specify a proc-name, a path
to a process pid file.  If the proc-name is present, the wccp client will
only advertise the associated service group, if the process is currently
up and running.  So if your computer is hosting three services, and one of
them goes down, the wccp client could stop advertising the service group
associated with the down service thus stopping the router from redirecting
that traffic, but continue to advertise and maintain the redirection for the
other two services.

The current WCCP implementation associated with ATS only supports one cache
client per service group per router.  The cache assignment logic current
assigns the current cache client to all buckets.
