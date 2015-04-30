All of these certificates are self-signed and are *not* secure. They are intended
only for use in testing.

Try to use existing certs if possible rather than generating your own.

.. code-block:: cmd

  # download the bash script
  $ mkdir certs && cd certs
  $ wget https://git.fedorahosted.org/cgit/pkinit-nss.git/plain/doc/openssl/make-certs.sh
  $ chmod u+x make-certs.sh
  
  # generate Intermediate CA Certificate
  $ ./make-certs.sh "Test Certifying Intermediate CA" test@example ca
  
  # generate leaf certificate
  $ mv "Test Certifying Intermediate CA" intermediate && cd intermediate
  $ ./make-certs.sh www.example.com test@example.com *.example.com example.com
  $ ./make-certs.sh www.example.org test@example.org *.example.org example.org
  
  # copy certificate and key files
  $ cd ../../
  $ cp certs/ca.crt certs/intermediate/ca.chain.crt certs/intermediate/www.example.*.pem ./
  $ cp certs/intermediate/ca.crt intermediate.crt
  
  # clean up
  $ rm -rf certs
