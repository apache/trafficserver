All of these certificates are self-signed and are *not* secure. They are intended
only for use in testing.

Try to use existing certs if possible rather than generating your own.

# generated using (make sure to set "hostname"):
openssl ecparam -name prime256v1 -genkey -out key.pem
openssl req -new -x509 -key key.pem -out cert.pem


## Since we want to verify all of the certificate verification, we need to generate
## our own CA and intermediate CA
# Create CA
openssl ecparam -name prime256v1 -genkey -out ca.key
openssl req -new -x509 -nodes -sha1 -days 1825 -key ca.key -out ca.crt

# Create Intermediate
openssl ecparam -name prime256v1 -genkey -out intermediate.key
openssl req -new -sha1 -key intermediate.key -out intermediate.csr

# CA signs Intermediate
openssl x509 -req -days 1825 -in intermediate.csr -CA ca.crt -CAkey ca.key -set_serial 01 -out intermediate.crt

# Create Server
openssl ecparam -name prime256v1 -genkey -out www.example.com.key
openssl req -new -key test.example.com.key -out test.example.com.csr

# Intermediate signs Server
openssl x509 -req -days 1825 -in test.example.com.csr -CA intermediate.crt -CAkey intermediate.key -set_serial 01 -out test.example.com.crt
