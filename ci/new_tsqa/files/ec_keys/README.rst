All of these certificates are self-signed and are *not* secure. They are intended
only for use in testing.

Try to use existing certs if possible rather than generating your own.

# generated using (make sure to set "hostname"):
openssl ecparam -name prime256v1 -genkey -out key.pem
openssl req -new -x509 -key key.pem -out cert.pem
