/*
 * GnuTLS PKCS#11 support
 * Copyright (C) 2010,2011 Free Software Foundation
 * 
 * Authors: Nikos Mavrogiannopoulos, Stef Walter
 *
 * The GnuTLS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <gnutls_int.h>
#include <gnutls/pkcs11.h>
#include <stdio.h>
#include <string.h>
#include <gnutls_errors.h>
#include <gnutls_datum.h>
#include <pkcs11_int.h>
#include <gnutls_sig.h>
#include <p11-kit/uri.h>

struct gnutls_pkcs11_privkey_st
{
  gnutls_pk_algorithm_t pk_algorithm;
  unsigned int flags;
  struct p11_kit_uri *info;
  gnutls_pkcs11_pin_callback_t pin_func;
  void *pin_data;
};

/**
 * gnutls_pkcs11_privkey_init:
 * @key: The structure to be initialized
 *
 * This function will initialize an private key structure.
 *
 * Returns: On success, %GNUTLS_E_SUCCESS (0) is returned, otherwise a
 *   negative error value.
 **/
int
gnutls_pkcs11_privkey_init (gnutls_pkcs11_privkey_t * key)
{
  *key = gnutls_calloc (1, sizeof (struct gnutls_pkcs11_privkey_st));
  if (*key == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  (*key)->info = p11_kit_uri_new ();
  if ((*key)->info == NULL)
    {
      free (*key);
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  return 0;
}

/**
 * gnutls_pkcs11_privkey_deinit:
 * @key: The structure to be initialized
 *
 * This function will deinitialize a private key structure.
 **/
void
gnutls_pkcs11_privkey_deinit (gnutls_pkcs11_privkey_t key)
{
  p11_kit_uri_free (key->info);
  gnutls_free (key);
}

/**
 * gnutls_pkcs11_privkey_get_pk_algorithm:
 * @key: should contain a #gnutls_pkcs11_privkey_t structure
 * @bits: if bits is non null it will hold the size of the parameters' in bits
 *
 * This function will return the public key algorithm of a private
 * key.
 *
 * Returns: a member of the #gnutls_pk_algorithm_t enumeration on
 *   success, or a negative error code on error.
 **/
int
gnutls_pkcs11_privkey_get_pk_algorithm (gnutls_pkcs11_privkey_t key,
                                        unsigned int *bits)
{
  if (bits)
    *bits = 0;                  /* FIXME */
  return key->pk_algorithm;
}

/**
 * gnutls_pkcs11_privkey_get_info:
 * @pkey: should contain a #gnutls_pkcs11_privkey_t structure
 * @itype: Denotes the type of information requested
 * @output: where output will be stored
 * @output_size: contains the maximum size of the output and will be overwritten with actual
 *
 * This function will return information about the PKCS 11 private key such
 * as the label, id as well as token information where the key is stored. When
 * output is text it returns null terminated string although #output_size contains
 * the size of the actual data only.
 *
 * Returns: %GNUTLS_E_SUCCESS (0) on success or a negative error code on error.
 **/
int
gnutls_pkcs11_privkey_get_info (gnutls_pkcs11_privkey_t pkey,
                                gnutls_pkcs11_obj_info_t itype,
                                void *output, size_t * output_size)
{
  return pkcs11_get_info (pkey->info, itype, output, output_size);
}


#define FIND_OBJECT(module, pks, obj, key) \
	do { \
		int retries = 0; \
		int rret; \
		ret = pkcs11_find_object (&module, &pks, &obj, key->info, \
			SESSION_LOGIN); \
		if (ret == GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE) { \
			if (token_func) \
			  { \
			    rret = pkcs11_call_token_func (key->info, retries++); \
			    if (rret == 0) continue; \
                          } \
			return gnutls_assert_val(ret); \
		} else if (ret < 0) { \
                        return gnutls_assert_val(ret); \
                } \
	} while (0);

/*-
 * _gnutls_pkcs11_privkey_sign_hash:
 * @key: Holds the key
 * @hash: holds the data to be signed (should be output of a hash)
 * @signature: will contain the signature allocated with gnutls_malloc()
 *
 * This function will sign the given data using a signature algorithm
 * supported by the private key. It is assumed that the given data
 * are the output of a hash function.
 *
 * Returns: On success, %GNUTLS_E_SUCCESS (0) is returned, otherwise a
 *   negative error value.
 -*/
int
_gnutls_pkcs11_privkey_sign_hash (gnutls_pkcs11_privkey_t key,
                                  const gnutls_datum_t * hash,
                                  gnutls_datum_t * signature)
{
  ck_rv_t rv;
  int ret;
  struct ck_mechanism mech;
  unsigned long siglen;
  struct ck_function_list *module;
  ck_session_handle_t pks;
  ck_object_handle_t obj;

  FIND_OBJECT (module, pks, obj, key);

  mech.mechanism = pk_to_mech(key->pk_algorithm);
  mech.parameter = NULL;
  mech.parameter_len = 0;

  /* Initialize signing operation; using the private key discovered
   * earlier. */
  rv = pkcs11_sign_init (module, pks, &mech, obj);
  if (rv != CKR_OK)
    {
      gnutls_assert ();
      ret = pkcs11_rv_to_err (rv);
      goto cleanup;
    }

  /* Work out how long the signature must be: */
  rv = pkcs11_sign (module, pks, hash->data, hash->size, NULL, &siglen);
  if (rv != CKR_OK)
    {
      gnutls_assert ();
      ret = pkcs11_rv_to_err (rv);
      goto cleanup;
    }

  signature->data = gnutls_malloc (siglen);
  signature->size = siglen;

  rv = pkcs11_sign (module, pks, hash->data, hash->size, signature->data, &siglen);
  if (rv != CKR_OK)
    {
      gnutls_free (signature->data);
      gnutls_assert ();
      ret = pkcs11_rv_to_err (rv);
      goto cleanup;
    }

  signature->size = siglen;

  ret = 0;

cleanup:
  pkcs11_close_session (module, pks);

  return ret;
}

/**
 * gnutls_pkcs11_privkey_import_url:
 * @pkey: The structure to store the parsed key
 * @url: a PKCS 11 url identifying the key
 * @flags: sequence of GNUTLS_PKCS_PRIVKEY_*
 *
 * This function will "import" a PKCS 11 URL identifying a private
 * key to the #gnutls_pkcs11_privkey_t structure. In reality since
 * in most cases keys cannot be exported, the private key structure
 * is being associated with the available operations on the token.
 *
 * Returns: On success, %GNUTLS_E_SUCCESS (0) is returned, otherwise a
 *   negative error value.
 **/
int
gnutls_pkcs11_privkey_import_url (gnutls_pkcs11_privkey_t pkey,
                                  const char *url, unsigned int flags)
{
  int ret;
  struct ck_function_list *module;
  struct ck_attribute *attr;
  ck_session_handle_t pks;
  ck_object_handle_t obj;
  struct ck_attribute a[4];
  ck_key_type_t key_type;

  ret = pkcs11_url_to_info (url, &pkey->info);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  pkey->flags = flags;

  attr = p11_kit_uri_get_attribute (pkey->info, CKA_CLASS);
  if (!attr || attr->value_len != sizeof (ck_object_class_t) ||
      *(ck_object_class_t*)attr->value != CKO_PRIVATE_KEY)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  attr = p11_kit_uri_get_attribute (pkey->info, CKA_ID);
  if (!attr || !attr->value_len)
    {
      gnutls_assert ();
      return GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE;
    }

  FIND_OBJECT (module, pks, obj, pkey);
  a[0].type = CKA_KEY_TYPE;
  a[0].value = &key_type;
  a[0].value_len = sizeof (key_type);

  if (pkcs11_get_attribute_value (module, pks, obj, a, 1) == CKR_OK)
    {
      pkey->pk_algorithm = mech_to_pk(key_type);
      if (pkey->pk_algorithm == GNUTLS_PK_UNKNOWN)
        {
          _gnutls_debug_log("Cannot determine PKCS #11 key algorithm\n");
          ret = GNUTLS_E_UNKNOWN_ALGORITHM;
          goto cleanup;
        }
    }

  ret = 0;

cleanup:
  pkcs11_close_session (module, pks);

  return ret;
}

/*-
 * _gnutls_pkcs11_privkey_decrypt_data:
 * @key: Holds the key
 * @flags: should be 0 for now
 * @ciphertext: holds the data to be signed
 * @plaintext: will contain the plaintext, allocated with gnutls_malloc()
 *
 * This function will decrypt the given data using the public key algorithm
 * supported by the private key. 
 *
 * Returns: On success, %GNUTLS_E_SUCCESS (0) is returned, otherwise a
 *   negative error value.
 -*/
int
_gnutls_pkcs11_privkey_decrypt_data (gnutls_pkcs11_privkey_t key,
                                    unsigned int flags,
                                    const gnutls_datum_t * ciphertext,
                                    gnutls_datum_t * plaintext)
{
  ck_rv_t rv;
  int ret;
  struct ck_mechanism mech;
  unsigned long siglen;
  struct ck_function_list *module;
  ck_session_handle_t pks;
  ck_object_handle_t obj;

  FIND_OBJECT (module, pks, obj, key);

  if (key->pk_algorithm != GNUTLS_PK_RSA)
    return gnutls_assert_val(GNUTLS_E_INVALID_REQUEST);

  mech.mechanism = CKM_RSA_PKCS;
  mech.parameter = NULL;
  mech.parameter_len = 0;

  /* Initialize signing operation; using the private key discovered
   * earlier. */
  rv = pkcs11_decrypt_init (module, pks, &mech, obj);
  if (rv != CKR_OK)
    {
      gnutls_assert ();
      ret = pkcs11_rv_to_err (rv);
      goto cleanup;
    }

  /* Work out how long the plaintext must be: */
  rv = pkcs11_decrypt (module, pks, ciphertext->data, ciphertext->size,
                         NULL, &siglen);
  if (rv != CKR_OK)
    {
      gnutls_assert ();
      ret = pkcs11_rv_to_err (rv);
      goto cleanup;
    }

  plaintext->data = gnutls_malloc (siglen);
  plaintext->size = siglen;

  rv = pkcs11_decrypt (module, pks, ciphertext->data, ciphertext->size,
                         plaintext->data, &siglen);
  if (rv != CKR_OK)
    {
      gnutls_free (plaintext->data);
      gnutls_assert ();
      ret = pkcs11_rv_to_err (rv);
      goto cleanup;
    }

  plaintext->size = siglen;

  ret = 0;

cleanup:
  pkcs11_close_session (module, pks);

  return ret;
}

/**
 * gnutls_pkcs11_privkey_export_url:
 * @key: Holds the PKCS 11 key
 * @detailed: non zero if a detailed URL is required
 * @url: will contain an allocated url
 *
 * This function will export a URL identifying the given key.
 *
 * Returns: On success, %GNUTLS_E_SUCCESS (0) is returned, otherwise a
 *   negative error value.
 **/
int
gnutls_pkcs11_privkey_export_url (gnutls_pkcs11_privkey_t key,
                                  gnutls_pkcs11_url_type_t detailed,
                                  char **url)
{
  int ret;

  ret = pkcs11_info_to_url (key->info, detailed, url);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  return 0;
}


/**
 * gnutls_pkcs11_privkey_generate:
 * @url: a token URL
 * @pk: the public key algorithm
 * @bits: the security bits
 * @label: a label
 * @flags: should be zero
 *
 * This function will generate a private key in the specified
 * by the @url token. The pivate key will be generate within
 * the token and will not be exportable.
 *
 * Returns: On success, %GNUTLS_E_SUCCESS (0) is returned, otherwise a
 *   negative error value.
 *
 * Since: 3.0.0
 **/
int
gnutls_pkcs11_privkey_generate (const char* url, 
  gnutls_pk_algorithm_t pk, unsigned int bits, 
  const char* label, unsigned int flags)
{
  int ret;
  const ck_bool_t tval = 1;
  const ck_bool_t fval = 0;
  struct ck_function_list *module;
  ck_session_handle_t pks = 0;
  struct p11_kit_uri *info = NULL;
  ck_rv_t rv;
  struct ck_attribute a[10], p[10];
  ck_object_handle_t pub, priv;
  unsigned long _bits = bits;
  int a_val, p_val;
  struct ck_mechanism mech;

  ret = pkcs11_url_to_info (url, &info);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  ret =
    pkcs11_open_session (&module, &pks, info,
                         SESSION_WRITE | pkcs11_obj_flags_to_int (flags));
  p11_kit_uri_free (info);

  if (ret < 0)
    {
      gnutls_assert ();
      goto cleanup;
    }

  /* a holds the public key template
   * and p the private key */
  a_val = p_val = 0;
  mech.parameter = NULL;
  mech.parameter_len = 0;
  mech.mechanism = pk_to_genmech(pk);

  switch(pk)
    {
      case GNUTLS_PK_RSA:
        p[p_val].type = CKA_DECRYPT;
        p[p_val].value = (void*)&tval;
        p[p_val].value_len = sizeof (tval);
        p_val++;

        p[p_val].type = CKA_SIGN;
        p[p_val].value = (void*)&tval;
        p[p_val].value_len = sizeof (tval);
        p_val++;

        a[a_val].type = CKA_ENCRYPT;
        a[a_val].value = (void*)&tval;
        a[a_val].value_len = sizeof (tval);
        a_val++;

        a[a_val].type = CKA_VERIFY;
        a[a_val].value = (void*)&tval;
        a[a_val].value_len = sizeof (tval);
        a_val++;

        a[a_val].type = CKA_MODULUS_BITS;
        a[a_val].value = &_bits;
        a[a_val].value_len = sizeof (_bits);
        a_val++;
        break;
      case GNUTLS_PK_DSA:
        p[p_val].type = CKA_SIGN;
        p[p_val].value = (void*)&tval;
        p[p_val].value_len = sizeof (tval);
        p_val++;

        a[a_val].type = CKA_VERIFY;
        a[a_val].value = (void*)&tval;
        a[a_val].value_len = sizeof (tval);
        a_val++;

        a[a_val].type = CKA_MODULUS_BITS;
        a[a_val].value = &_bits;
        a[a_val].value_len = sizeof (_bits);
        a_val++;
        break;
      case GNUTLS_PK_EC:
        p[p_val].type = CKA_SIGN;
        p[p_val].value = (void*)&tval;
        p[p_val].value_len = sizeof (tval);
        p_val++;

        a[a_val].type = CKA_VERIFY;
        a[a_val].value = (void*)&tval;
        a[a_val].value_len = sizeof (tval);
        a_val++;

        a[a_val].type = CKA_MODULUS_BITS;
        a[a_val].value = &_bits;
        a[a_val].value_len = sizeof (_bits);
        a_val++;
        break;
      default:
        ret = gnutls_assert_val(GNUTLS_E_INVALID_REQUEST);
        goto cleanup;
    }

  /* a private key is set always as private unless
   * requested otherwise
   */
  if (flags & GNUTLS_PKCS11_OBJ_FLAG_MARK_NOT_PRIVATE)
    {
      p[p_val].type = CKA_PRIVATE;
      p[p_val].value = (void*)&fval;
      p[p_val].value_len = sizeof(fval);
      p_val++;
    }
  else
    {
      p[p_val].type = CKA_PRIVATE;
      p[p_val].value = (void*)&tval;
      p[p_val].value_len = sizeof (tval);
      p_val++;
    }

  p[p_val].type = CKA_TOKEN;
  p[p_val].value = (void *)&tval;
  p[p_val].value_len = sizeof (tval);
  p_val++;

  if (label)
    {
      p[p_val].type = CKA_LABEL;
      p[p_val].value = (void*)label;
      p[p_val].value_len = strlen (label);
      p_val++;

      a[a_val].type = CKA_LABEL;
      a[a_val].value = (void*)label;
      a[a_val].value_len = strlen (label);
      a_val++;
    }

  if (flags & GNUTLS_PKCS11_OBJ_FLAG_MARK_SENSITIVE)
    {
      p[p_val].type = CKA_SENSITIVE;
      p[p_val].value = (void*)&tval;
      p[p_val].value_len = sizeof (tval);
      p_val++;
    }
  else
    {
      p[p_val].type = CKA_SENSITIVE;
      p[p_val].value = (void*)&fval;
      p[p_val].value_len = sizeof (fval);
      p_val++;
    }

  rv = pkcs11_generate_key_pair( module, pks, &mech, a, a_val, p, p_val, &pub, &priv);
  if (rv != CKR_OK)
    {
      gnutls_assert ();
      _gnutls_debug_log ("pkcs11: %s\n", pkcs11_strerror (rv));
      ret = pkcs11_rv_to_err (rv);
      goto cleanup;
    }
    

cleanup:
  if (pks != 0)
    pkcs11_close_session (module, pks);

  return ret;
}
