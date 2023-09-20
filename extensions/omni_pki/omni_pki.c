/**
 * @file omni_vfs.c
 *
 */

// clang-format off
#include <postgres.h>
#include <fmgr.h>
// clang-format on
#include <catalog/pg_enum.h>
#include <executor/spi.h>
#include <utils/syscache.h>

PG_MODULE_MAGIC;

#include "libpgaug.h"
#include "utils/builtins.h"

#include <openssl/pem.h>
#include <openssl/x509.h>

PG_FUNCTION_INFO_V1(generate_certificate);

Datum generate_certificate(PG_FUNCTION_ARGS) {
  EVP_PKEY *pkey = NULL;
  EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);

  EVP_PKEY_keygen_init(pctx);
  EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048);
  EVP_PKEY_keygen(pctx, &pkey);

  X509 *x509 = X509_new();

  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // Valid for one year

  X509_set_pubkey(x509, pkey);

  X509_NAME *name = X509_get_subject_name(x509);

  X509_NAME_add_entry_by_txt(name, "C",  MBSTRING_ASC, (unsigned char *)"US", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O",  MBSTRING_ASC, (unsigned char *)"Your Organisation", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"Your Common Name", -1, -1, 0);

  X509_set_issuer_name(x509, name); // Set issuer name

  X509_sign(x509, pkey, EVP_sha256());

  // Write certificate to memory
  BIO *bio = BIO_new(BIO_s_mem());
  PEM_write_bio_X509(bio, x509);

  // Write the contents of the BIO to a string
  char *cert_str = NULL;
  long cert_len = BIO_get_mem_data(bio, &cert_str);

  text *cert = cstring_to_text_with_len(cert_str, cert_len);

  // You now have a pointer to the certificate in PEM format in cert_str
  // The length of the data is stored in cert_len

  // Do whatever you want with the string here
  // ...

  // Cleanup
  BIO_free(bio);
  X509_free(x509);
  EVP_PKEY_free(pkey);
  EVP_PKEY_CTX_free(pctx);

  PG_RETURN_TEXT_P(cert);
}