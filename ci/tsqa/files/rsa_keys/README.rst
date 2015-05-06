All of these certificates are self-signed and are *not* secure. They are intended
only for use in testing.

Try to use existing certs if possible rather than generating your own.

# generated using (make sure to set "hostname"):
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -nodes && cat key.pem cert.pem > keypair.pem && rm key.pem cert.pem


## Since we want to verify all of the certificate verification, we need to generate
## our own CA and intermediate CA
# Create CA
openssl genrsa -out ca.key 4096
openssl req -new -x509 -nodes -sha1 -days 1825 -key ca.key -out ca.crt

# Create Intermediate
openssl genrsa -out intermediate.key 4096
openssl req -new -sha1 -key intermediate.key -out intermediate.csr

# CA signs Intermediate
openssl x509 -req -days 1825 -in intermediate.csr -CA ca.crt -CAkey ca.key -set_serial 01 -out intermediate.crt

# Create Server
openssl genrsa -out test.example.com.key 4096
openssl req -new -key test.example.com.key -out test.example.com.csr

# Intermediate signs Server
openssl x509 -req -days 1825 -in test.example.com.csr -CA intermediate.crt -CAkey intermediate.key -set_serial 01 -out test.example.com.crt
