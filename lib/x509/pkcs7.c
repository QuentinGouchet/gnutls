/*
 *  Copyright (C) 2003 Nikos Mavroyanopoulos
 *
 *  This file is part of GNUTLS.
 *
 *  The GNUTLS library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public   
 *  License as published by the Free Software Foundation; either 
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 */

/* Functions that relate on PKCS7 certificate lists parsing.
 */

#include <libtasn1.h>
#include <gnutls_int.h>
#include <gnutls_datum.h>
#include <gnutls_global.h>
#include <gnutls_errors.h>
#include <common.h>
#include <x509_b64.h>
#include <pkcs7.h>
#include <dn.h>

#define SIGNED_DATA_OID "1.2.840.113549.1.7.2"

/**
  * gnutls_pkcs7_init - This function initializes a gnutls_pkcs7 structure
  * @pkcs7: The structure to be initialized
  *
  * This function will initialize a PKCS7 structure. 
  *
  * Returns 0 on success.
  *
  **/
int gnutls_pkcs7_init(gnutls_pkcs7 * pkcs7)
{
	*pkcs7 = gnutls_calloc( 1, sizeof(gnutls_pkcs7_int));

	if (*pkcs7) {
		int result = asn1_create_element(_gnutls_get_pkix(),
				     "PKIX1.ContentInfo",
				     &(*pkcs7)->pkcs7);
		if (result != ASN1_SUCCESS) {
			gnutls_assert();
			return _gnutls_asn2err(result);
		}
		return 0;		/* success */
	}
	return GNUTLS_E_MEMORY_ERROR;
}

/**
  * gnutls_pkcs7_deinit - This function deinitializes memory used by a gnutls_pkcs7 structure
  * @pkcs7: The structure to be initialized
  *
  * This function will deinitialize a PKCS7 structure. 
  *
  **/
void gnutls_pkcs7_deinit(gnutls_pkcs7 pkcs7)
{
	if (pkcs7->pkcs7)
		asn1_delete_structure(&pkcs7->pkcs7);

	gnutls_free(pkcs7);
}

/**
  * gnutls_pkcs7_import - This function will import a DER or PEM encoded PKCS7
  * @pkcs7: The structure to store the parsed PKCS7.
  * @data: The DER or PEM encoded PKCS7.
  * @format: One of DER or PEM
  *
  * This function will convert the given DER or PEM encoded PKCS7
  * to the native gnutls_pkcs7 format. The output will be stored in 'pkcs7'.
  *
  * If the PKCS7 is PEM encoded it should have a header of "PKCS7".
  *
  * Returns 0 on success.
  *
  **/
int gnutls_pkcs7_import(gnutls_pkcs7 pkcs7, const gnutls_datum * data,
	gnutls_x509_crt_fmt format)
{
	int result = 0, need_free = 0;
	gnutls_datum _data = { data->data, data->size };

	/* If the PKCS7 is in PEM format then decode it
	 */
	if (format == GNUTLS_X509_FMT_PEM) {
		opaque *out;
		
		result = _gnutls_fbase64_decode(PEM_PKCS7, data->data, data->size,
			&out);

		if (result <= 0) {
			if (result==0) result = GNUTLS_E_INTERNAL_ERROR;
			gnutls_assert();
			return result;
		}
		
		_data.data = out;
		_data.size = result;
		
		need_free = 1;
	}


	result = asn1_der_decoding(&pkcs7->pkcs7, _data.data, _data.size, NULL);
	if (result != ASN1_SUCCESS) {
		result = _gnutls_asn2err(result);
		gnutls_assert();
		goto cleanup;
	}

	if (need_free) _gnutls_free_datum( &_data);

	return 0;

      cleanup:
	if (need_free) _gnutls_free_datum( &_data);
	return result;
}


/**
  * gnutls_pkcs7_get_certificate - This function returns a certificate in a PKCS7 certificate set
  * @pkcs7_struct: should contain a gnutls_pkcs7 structure
  * @indx: contains the index of the certificate to extract
  * @certificate: the contents of the certificate will be copied there (may be null)
  * @certificate_size: should hold the size of the certificate
  *
  * This function will return a certificate of the PKCS7 or RFC2630 certificate set.
  * Returns 0 on success. If the provided buffer is not long enough,
  * then GNUTLS_E_SHORT_MEMORY_BUFFER is returned.
  *
  * After the last certificate has been read GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE
  * will be returned.
  *
  **/
int gnutls_pkcs7_get_certificate(gnutls_pkcs7 pkcs7, 
	int indx, unsigned char* certificate, int* certificate_size)
{
	ASN1_TYPE c2 = ASN1_TYPE_EMPTY;
	int result, len;
	char oid[128];
	opaque *tmp = NULL;
	char root2[64];
	char counter[MAX_INT_DIGITS];
	int tmp_size;

	if (certificate_size == NULL) return GNUTLS_E_INVALID_REQUEST;

	/* root2 is used as a temp storage area
	 */
	len = sizeof(oid) - 1;
	result = asn1_read_value(pkcs7->pkcs7, "contentType", oid, &len);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		return _gnutls_asn2err(result);
	}

	/* id-signedData as defined in PKCS #7 
	 */
	if ( strcmp( oid, SIGNED_DATA_OID) != 0) {
		gnutls_assert();
		_gnutls_x509_log( "Unknown PKCS7 Content OID '%s'\n", oid);
		return GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE;
	}					 		 	

	tmp_size = 0;
	result = asn1_read_value(pkcs7->pkcs7, "content", NULL, &tmp_size);
	if (result!=ASN1_MEM_ERROR) {
		gnutls_assert();
		return _gnutls_asn2err(result);
	}

	tmp = gnutls_malloc(tmp_size);
	if (tmp==NULL) {
		gnutls_assert();
		return GNUTLS_E_MEMORY_ERROR;
	}

	result = asn1_read_value(pkcs7->pkcs7, "content", tmp, &tmp_size);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	/* tmp, tmp_size hold the data and the size of the CertificateSet structure
	 * actually the ANY stuff.
	 */

	/* Step 1. In case of a signed structure extract certificate set.
	 */
	if ((result=asn1_create_element
	    (_gnutls_get_pkix(), "PKIX1.SignedData", &c2)) != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	}

	result = asn1_der_decoding(&c2, tmp, tmp_size, NULL);
	if (result != ASN1_SUCCESS) {
		/* couldn't decode DER */
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	
	}
	
	/* Step 2. Parse the CertificateSet 
	 */
	
	_gnutls_str_cpy( root2, sizeof(root2), "certificates.?"); 
	_gnutls_int2str( indx+1, counter);
	_gnutls_str_cat( root2, sizeof(root2), counter); 

	len = sizeof(oid) - 1;

	result = asn1_read_value(c2, root2, oid, &len);

	if (result == ASN1_VALUE_NOT_FOUND) {
		result = GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE;
		goto cleanup;	
	}
	
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	
	}

	/* if 'Certificate' is the choice found: 
	 */
	if (strcmp( oid, "certificate") == 0) {
		int start, end;

		result = asn1_der_decoding_startEnd(c2, tmp, tmp_size, 
			root2, &start, &end);

		if (result != ASN1_SUCCESS) {
			gnutls_assert();
			result = _gnutls_asn2err(result);
			goto cleanup;
		}
			
		end = end-start+1;
		
		if ( end > *certificate_size) {
			*certificate_size = end;
			result = GNUTLS_E_SHORT_MEMORY_BUFFER;
			goto cleanup;
		}

		if (certificate)
			memcpy( certificate, &tmp[start], end);

		*certificate_size = end;

		result = 0;

	} else {
		result = GNUTLS_E_UNSUPPORTED_CERTIFICATE_TYPE;
	}

	cleanup:
		if (c2) asn1_delete_structure(&c2);
		gnutls_free(tmp);
		return result;
}


/**
  * gnutls_pkcs7_get_certificate_count - This function returns the number of certificates in a PKCS7 certificate set
  * @pkcs7_struct: should contain a gnutls_pkcs7 structure
  *
  * This function will return the number of certifcates in the PKCS7 or 
  * RFC2630 certificate set.
  *
  * Returns a negative value on failure.
  *
  **/
int gnutls_pkcs7_get_certificate_count(gnutls_pkcs7 pkcs7)
{
	ASN1_TYPE c2 = ASN1_TYPE_EMPTY;
	int result, len, count;
	char oid[64];
	opaque *tmp = NULL;
	int tmp_size;

	len = sizeof(oid) - 1;

	/* root2 is used as a temp storage area
	 */
	result = asn1_read_value(pkcs7->pkcs7, "contentType", oid, &len);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		return _gnutls_asn2err(result);
	}

	if ( strcmp( oid, SIGNED_DATA_OID) != 0) {
		gnutls_assert();
		_gnutls_x509_log( "Unknown PKCS7 Content OID '%s'\n", oid);
		return GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE;
	}					 		 	

	tmp_size = 0;
	result = asn1_read_value(pkcs7->pkcs7, "content", NULL, &tmp_size);
	if (result!=ASN1_MEM_ERROR) {
		gnutls_assert();
		return _gnutls_asn2err(result);
	}

	tmp = gnutls_malloc(tmp_size);
	if (tmp==NULL) {
		gnutls_assert();
		return GNUTLS_E_MEMORY_ERROR;
	}

	result = asn1_read_value(pkcs7->pkcs7, "content", tmp, &tmp_size);
	if (result!=ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	/* tmp, tmp_size hold the data and the size of the CertificateSet structure
	 * actually the ANY stuff.
	 */

	/* Step 1. In case of a signed structure count the certificate set.
	 */
	if ((result=asn1_create_element
	    (_gnutls_get_pkix(), "PKIX1.SignedData", &c2)) != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	result = asn1_der_decoding(&c2, tmp, tmp_size, NULL);
	if (result != ASN1_SUCCESS) {
		/* couldn't decode DER */
	
		gnutls_assert();
		asn1_delete_structure(&c2);
		result = _gnutls_asn2err(result);
		goto cleanup;
	}
		
	gnutls_free(tmp);
	tmp = NULL;

	/* Step 2. Count the CertificateSet */
	
	result = asn1_number_of_elements( c2, "certificates", &count);

	asn1_delete_structure(&c2);
	
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		return 0; /* no certificates */
	}

	return count;
	
	cleanup:
		gnutls_free(tmp);
		return result;
}

/**
  * gnutls_pkcs7_export - This function will export the pkcs7 structure
  * @pkcs7: Holds the pkcs7 structure
  * @format: the format of output params. One of PEM or DER.
  * @output_data: will contain a structure PEM or DER encoded
  * @output_data_size: holds the size of output_data (and will be replaced by the actual size of parameters)
  *
  * This function will export the pkcs7 structure to DER or PEM format.
  *
  * If the buffer provided is not long enough to hold the output, then
  * GNUTLS_E_SHORT_MEMORY_BUFFER will be returned.
  *
  * If the structure is PEM encoded, it will have a header
  * of "BEGIN CERTIFICATE".
  *
  * In case of failure a negative value will be returned, and
  * 0 on success.
  *
  **/
int gnutls_pkcs7_export( gnutls_pkcs7 pkcs7,
	gnutls_x509_crt_fmt format, unsigned char* output_data, int* output_data_size)
{
	return _gnutls_x509_export_int( pkcs7->pkcs7, format, PEM_PKCS7, *output_data_size,
		output_data, output_data_size);
}


static int create_empty_signed_data(ASN1_TYPE pkcs7)
{
	ASN1_TYPE c2 = ASN1_TYPE_EMPTY;
	uint8 one = 1;
	int result;

	if ((result=asn1_create_element
	    (_gnutls_get_pkix(), "PKIX1.SignedData", &c2)) != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	/* Use version 1
	 */
	result = asn1_write_value( c2, "version", &one, 1);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	/* Use no digest algorithms
	 */

	/* id-data */
	result = asn1_write_value( c2, "encapContentInfo.eContentType", "1.2.840.113549.1.7.5", 1);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	result = asn1_write_value( c2, "encapContentInfo.eContent", NULL, 0);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	/* Add no certificates.
	 */

	/* Add no crls.
	 */

	/* Add no signerInfos.
	 */

	/* Copy the signed data to the pkcs7
	 */
	result = _gnutls_x509_der_encode_and_copy( c2, "", pkcs7, "content");
	if (result < 0) {
		gnutls_assert();
		goto cleanup;
	}
	asn1_delete_structure( &c2);

	/* Write the content type of the signed data
	 */
	result = asn1_write_value(pkcs7, "contentType", SIGNED_DATA_OID, 1);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	return 0;

	cleanup:
		asn1_delete_structure( &c2);
		return result;	

}

/**
  * gnutls_pkcs7_set_certificate - This function adds a certificate in a PKCS7 certificate set
  * @pkcs7_struct: should contain a gnutls_pkcs7 structure
  * @crt: the DER encoded certificate to be added
  *
  * This function will add a certificate to the PKCS7 or RFC2630 certificate set.
  * Returns 0 on success.
  *
  **/
int gnutls_pkcs7_set_certificate(gnutls_pkcs7 pkcs7, 
	const gnutls_datum* crt)
{
	ASN1_TYPE c2 = ASN1_TYPE_EMPTY;
	int result, len;
	char oid[128];
	opaque *tmp = NULL;
	int tmp_size;

	/* root2 is used as a temp storage area
	 */
	len = sizeof(oid) - 1;
	result = asn1_read_value(pkcs7->pkcs7, "contentType", oid, &len);
	if (result != ASN1_VALUE_NOT_FOUND && result != ASN1_SUCCESS) {
		gnutls_assert();
		return _gnutls_asn2err(result);
	}

	if (result == ASN1_VALUE_NOT_FOUND) {
		/* The pkcs7 structure is new, so create the
		 * signedData.
		 */
		result = create_empty_signed_data( pkcs7->pkcs7);
		if (result < 0) {
			gnutls_assert();
			return result;
		}
	} else { /* success */
		if ( strcmp( oid, SIGNED_DATA_OID) != 0) {
			gnutls_assert();
			_gnutls_x509_log( "Unknown PKCS7 Content OID '%s'\n", oid);
			return GNUTLS_E_UNKNOWN_PKCS7_CONTENT_TYPE;
		}
	}					 		 	

	if ((result=asn1_create_element
	    (_gnutls_get_pkix(), "PKIX1.SignedData", &c2)) != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	
	}
	
	/* the Signed-data has been created, so
	 * decode them.
	 */
	tmp_size = 0;
	result = asn1_read_value(pkcs7->pkcs7, "content", NULL, &tmp_size);
	if (result!=ASN1_MEM_ERROR) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	tmp = gnutls_malloc(tmp_size);
	if (tmp==NULL) {
		gnutls_assert();
		result = GNUTLS_E_MEMORY_ERROR;
		goto cleanup;
	}

	result = asn1_read_value(pkcs7->pkcs7, "content", tmp, &tmp_size);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;
	}

	/* tmp, tmp_size hold the data and the size of the CertificateSet structure
	 * actually the ANY stuff.
	 */

	/* Step 1. In case of a signed structure extract certificate set.
	 */

	result = asn1_der_decoding(&c2, tmp, tmp_size, NULL);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	
	}

	gnutls_free(tmp);
	tmp = NULL;

	/* Step 2. Append the new certificate.
	 */

	result = asn1_write_value(c2, "certificates", "NEW", 1);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	
	}

	result = asn1_write_value(c2, "certificates.?LAST", "certificate", 1);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	
	}

#error FIX THAT.
	result = asn1_write_value(c2, "certificates.?LAST.certificate", crt->data, crt->size);
	if (result != ASN1_SUCCESS) {
		gnutls_assert();
		result = _gnutls_asn2err(result);
		goto cleanup;	
	}

	/* Step 3. Replace the old content with the new
	 */
	result = _gnutls_x509_der_encode_and_copy( c2, "", pkcs7->pkcs7, "content");
	if (result < 0) {
		gnutls_assert();
		goto cleanup;
	}

	asn1_delete_structure(&c2);

	return 0;

	cleanup:
		if (c2) asn1_delete_structure(&c2);
		gnutls_free(tmp);
		return result;
}
