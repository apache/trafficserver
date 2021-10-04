# JSONRPC 2.0 Client API utility definitions.

All this definitions are meant to be used by clients of the JSONRPC node which
are looking to interact with it in a different C++ application, like traffic_ctl
and traffic_top. 
All this definitions under the shared::rpc namespace are a client lightweight
version of the ones used internally by the JSONRPC node server/handlers, they
should not be mixed with the ones defined in `mgmt2/rpc/jsonrpc` which are for 
internal use only.

