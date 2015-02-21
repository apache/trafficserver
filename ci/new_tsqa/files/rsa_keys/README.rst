All of these certificates are self-signed and are *not* secure. They are intended
only for use in testing.

Try to use existing certs if possible rather than generating your own.

# generated using (make sure to set "hostname"):
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -nodes && cat key.pem cert.pem > keypair.pem && rm key.pem cert.pem
