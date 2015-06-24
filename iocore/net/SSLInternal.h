#ifndef SSLINTERNAL_H_BEEN_INCLUDED_BEFORE
#define SSLINTERNAL_H_BEEN_INCLUDED_BEFORE
// Defined in SSLInteral.c

#if !TS_USE_SET_RBIO
void SSL_set_rbio(SSL *ssl, BIO *rbio);
#endif

unsigned char *SSL_get_session_ticket(SSLNetVConnection *sslvc);

size_t SSL_get_session_ticket_length(SSLNetVConnection *sslvc);

long SSL_get_session_ticket_lifetime_hint(SSLNetVConnection *sslvc);

void SSL_set_session_ticket(SSLNetVConnection *sslvc, void *ticket, size_t ticketLength);

#endif /*SSLINTERNAL_H_BEEN_INCLUDED_BEFORE */
