Usage:

In plugins.config,

disable_http2 sni_a sni_b [...]

For connections with any of these SNI values, HTTP/2 will be removed (disabled) from the list of valid next protocols.
