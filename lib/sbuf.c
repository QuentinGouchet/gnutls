/*
 * Copyright (C) 2013 Nikos Mavrogiannopoulos
 *
 * Author: Nikos Mavrogiannopoulos
 *
 * This file is part of GnuTLS.
 *
 * The gnutls library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 */

#include <gnutls_int.h>
#include <gnutls_errors.h>
#include <gnutls_num.h>
#include <gnutls/sbuf.h>

#include <sbuf.h>

/**
 * gnutls_sbuf_sinit:
 * @isb: is a pointer to a #gnutls_sbuf_t structure.
 * @session: a GnuTLS session
 * @flags: should be zero or %GNUTLS_SBUF_WRITE_FLUSHES
 *
 * This function initializes a #gnutls_sbuf_t structure associated
 * with the provided session. If the flag %GNUTLS_SBUF_WRITE_FLUSHES
 * is set then gnutls_sbuf_queue() will flush when the maximum
 * data size for a record is reached.
 *
 * Returns: %GNUTLS_E_SUCCESS on success, or an error code.
 *
 * Since: 3.1.7
 **/
int gnutls_sbuf_sinit (gnutls_sbuf_t * isb, gnutls_session_t session,
                       unsigned int flags)
{
struct gnutls_sbuf_st* sb;

  sb = gnutls_malloc(sizeof(*sb));
  if (sb == NULL)
    return gnutls_assert_val(GNUTLS_E_MEMORY_ERROR);

  _gnutls_buffer_init(&sb->buf);
  sb->session = session;
  sb->flags = flags;
  
  *isb = sb;
  
  return 0;
}


/**
 * gnutls_credentials_deinit:
 * @cred: is a #gnutls_credentials_t structure.
 *
 * This function deinitializes a #gnutls_credentials_t structure.
 *
 * Returns: %GNUTLS_E_SUCCESS on success, or an error code.
 *
 * Since: 3.1.7
 **/
void gnutls_credentials_deinit (gnutls_credentials_t cred)
{
  if (cred->xcred)
    gnutls_certificate_free_credentials(cred->xcred);
  gnutls_free(cred);
}

/**
 * gnutls_credentials_init:
 * @cred: is a pointer to a #gnutls_credentials_t structure.
 *
 * This function initializes a #gnutls_credentials_t structure.
 *
 * Returns: %GNUTLS_E_SUCCESS on success, or an error code.
 *
 * Since: 3.1.7
 **/
int gnutls_credentials_init (gnutls_credentials_t* cred)
{
  *cred = gnutls_calloc(1, sizeof(*cred));
  if (*cred == NULL)
    return gnutls_assert_val(GNUTLS_E_MEMORY_ERROR);
  
  return 0;
}

static int
_verify_certificate_callback (gnutls_session_t session)
{
  unsigned int status;
  gnutls_sbuf_t sb;
  int ret, type;
  const char *hostname = NULL;
  const char *service = NULL;
  const char *tofu_file = NULL;
  
  sb = gnutls_session_get_ptr(session);
  if (sb == NULL)
    return gnutls_assert_val(GNUTLS_E_INTERNAL_ERROR);

  if (sb->server_name[0] != 0)
    hostname = sb->server_name;

  if (sb->service_name[0] != 0)
    service = sb->service_name;

  if (sb->cred->tofu_file[0] != 0)
    tofu_file = sb->cred->tofu_file;

  /* This verification function uses the trusted CAs in the credentials
   * structure. So you must have installed one or more CA certificates.
   */
  if (sb->cred->vflags & GNUTLS_VMETHOD_SYSTEM_CAS || sb->cred->vflags & GNUTLS_VMETHOD_GIVEN_CAS)
    {
      ret = gnutls_certificate_verify_peers3 (session, hostname, &status);
      if (ret < 0)
        return gnutls_assert_val(GNUTLS_E_CERTIFICATE_ERROR);

      if (status != 0) /* Certificate is not trusted */
        return gnutls_assert_val(GNUTLS_E_CERTIFICATE_ERROR);
    }

  if (sb->cred->vflags & GNUTLS_VMETHOD_TOFU)
    {
      const gnutls_datum_t *cert_list;
      unsigned int cert_list_size;

      type = gnutls_certificate_type_get (session);

      /* Do SSH verification */
      cert_list = gnutls_certificate_get_peers (session, &cert_list_size);
      if (cert_list == NULL)
        return gnutls_assert_val(GNUTLS_E_CERTIFICATE_ERROR);

      /* service may be obtained alternatively using getservbyport() */
      ret = gnutls_verify_stored_pubkey(tofu_file, NULL, hostname, service, 
                                    type, &cert_list[0], 0);
      if (ret == GNUTLS_E_NO_CERTIFICATE_FOUND)
        {
          /* host was not seen before. Store the key */
          gnutls_store_pubkey(tofu_file, NULL, hostname, service, 
                              type, &cert_list[0], 0, 0);
        }
      else if (ret == GNUTLS_E_CERTIFICATE_KEY_MISMATCH)
        return gnutls_assert_val(GNUTLS_E_CERTIFICATE_ERROR);
      else if (ret < 0)
        return gnutls_assert_val(ret);
    }
  
  /* notify gnutls to continue handshake normally */
  return 0;
}

/**
 * gnutls_credentials_set_trust:
 * @cred: is a #gnutls_credentials_t structure.
 * @vflags: the requested peer verification methods
 * @aux: Auxilary data to input any required CA certificate etc.
 * @aux_size: the number of the auxillary data provided
 *
 * This function initializes X.509 certificates in 
 * a #gnutls_credentials_t structure.
 *
 * The @ca_file and @crl_file are required only if @vflags includes
 * %GNUTLS_VMETHOD_GIVEN_CAS. The @tofu_file may be set if 
 * %GNUTLS_VMETHOD_TOFU is specified.
 *
 * Returns: %GNUTLS_E_SUCCESS on success, or an error code.
 *
 * Since: 3.1.7
 **/
int gnutls_credentials_set_trust (gnutls_credentials_t cred, unsigned vflags, 
                                  gnutls_cinput_st* aux,
                                  unsigned aux_size)
{
int ret;
unsigned len, i;

  if (cred->xcred == NULL)
    {
      ret = gnutls_certificate_allocate_credentials(&cred->xcred);
      if (ret < 0)
        return gnutls_assert_val(ret);
    }
  
  if (vflags & GNUTLS_VMETHOD_SYSTEM_CAS)
    {
      ret = gnutls_certificate_set_x509_system_trust(cred->xcred);
      if (ret < 0)
        {
          gnutls_assert();
          goto fail1;
        }
    }

  if (vflags & GNUTLS_VMETHOD_GIVEN_CAS)
    {
      for (i=0;i<aux_size;i++)
        {
          if (aux[i].contents == GNUTLS_CINPUT_CAS && aux[i].type == GNUTLS_CRED_FILE)
            ret = gnutls_certificate_set_x509_trust_file(cred->xcred, aux[i].i.file, aux[i].fmt);
          else if (aux[i].contents == GNUTLS_CINPUT_CAS && aux[i].type == GNUTLS_CRED_MEM)
            ret = gnutls_certificate_set_x509_trust_mem(cred->xcred, &aux[i].i.mem, aux[i].fmt);
          else if (aux[i].contents == GNUTLS_CINPUT_CRLS && aux[i].type == GNUTLS_CRED_FILE)
            ret = gnutls_certificate_set_x509_crl_file(cred->xcred, aux[i].i.file, aux[i].fmt);
          else if (aux[i].contents == GNUTLS_CINPUT_CRLS && aux[i].type == GNUTLS_CRED_MEM)
            ret = gnutls_certificate_set_x509_crl_mem(cred->xcred, &aux[i].i.mem, aux[i].fmt);
          else
            ret = 0;
          if (ret < 0)
            {
              gnutls_assert();
              goto fail1;
            }
        }
    }

  if (vflags & GNUTLS_VMETHOD_TOFU)
    {
      for (i=0;i<aux_size;i++)
        {
          if (aux[i].contents == GNUTLS_CINPUT_TOFU_DB)
            {
              if (aux[i].type != GNUTLS_CRED_FILE)
                {
                  ret = gnutls_assert_val(GNUTLS_E_INVALID_REQUEST);
                  goto fail1;
                }
              len = strlen(aux[i].i.file);
      
              if (len >= sizeof(cred->tofu_file))
                {
                  ret = gnutls_assert_val(GNUTLS_E_INVALID_REQUEST);
                  goto fail1;
                }
              memcpy(cred->tofu_file, aux[i].i.file, len+1);
            }
        }
    }
  
  gnutls_certificate_set_verify_function (cred->xcred, _verify_certificate_callback);

  return 0;
fail1:
  gnutls_certificate_free_credentials(cred->xcred);
  cred->xcred = NULL;
  return ret;
}


/**
 * gnutls_sbuf_client_init:
 * @isb: is a pointer to a #gnutls_sbuf_t structure.
 * @hostname: The name of the host to connect to
 * @service: The name of the host to connect to
 * @fd: a socket descriptor
 * @priority: A priority string to use (use %NULL for default)
 * @cred: A credentials structure
 * @flags: should be zero or %GNUTLS_SBUF_WRITE_FLUSHES
 *
 * This function initializes a #gnutls_sbuf_t structure.
 * If the flag %GNUTLS_SBUF_WRITE_FLUSHES
 * is set then gnutls_sbuf_queue() will flush when the maximum
 * data size for a record is reached.
 *
 * Returns: %GNUTLS_E_SUCCESS on success, or an error code.
 *
 * Since: 3.1.7
 **/
int gnutls_sbuf_client_init (gnutls_sbuf_t * isb, const char* hostname, 
                             const char* service,
                             gnutls_transport_ptr fd, 
                             const char* priority, gnutls_credentials_t cred,
                             unsigned int flags)
{
struct gnutls_sbuf_st* sb;
gnutls_session_t session;
int ret;
unsigned len;

  ret = gnutls_init(&session, GNUTLS_CLIENT);
  if (ret < 0)
    return gnutls_assert_val(ret);

  sb = gnutls_calloc(1, sizeof(*sb));
  if (sb == NULL)
    {
      ret = gnutls_assert_val(GNUTLS_E_MEMORY_ERROR);
      goto fail1;
    }
  _gnutls_buffer_init(&sb->buf);
  sb->session = session;
  sb->flags = flags;
  
  /* set session/handshake info 
   */
  gnutls_handshake_set_timeout(session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);
  
  if (priority == NULL) priority = "NORMAL:%COMPAT";
  ret = gnutls_priority_set_direct(session, priority, NULL);
  if (ret < 0)
    {
      gnutls_assert();
      goto fail1;
    }
  
  if (cred->xcred)
    {
      ret = gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, cred->xcred);
      if (ret < 0)
        {
          gnutls_assert();
          goto fail1;
        }
    }

  if (hostname)
    {
      len = strlen(hostname);
      
      if (len >= sizeof(sb->server_name))
        return gnutls_assert_val(GNUTLS_E_INVALID_REQUEST);
      memcpy(sb->server_name, hostname, len+1);

      ret = gnutls_server_name_set(session, GNUTLS_NAME_DNS, hostname, len);
      if (ret < 0)
        {
          gnutls_assert();
          goto fail1;
        }
    }

  if (service)
    {
      len = strlen(service);
      
      if (len >= sizeof(sb->service_name))
        return gnutls_assert_val(GNUTLS_E_INVALID_REQUEST);
      memcpy(sb->service_name, service, len+1);
    }

  gnutls_transport_set_ptr (session, fd);
  gnutls_session_set_ptr( session, sb);
  
  do
    {
      ret = gnutls_handshake(session);
    }
  while (ret < 0 && gnutls_error_is_fatal (ret) == 0);

  if (ret < 0)
    {
      gnutls_assert();
      goto fail1;
    }
  
  *isb = sb;
  
  return 0;

fail1:
  if (sb)
    gnutls_sbuf_deinit(sb);
  gnutls_deinit(session);
  
  return ret;
}

/**
 * gnutls_sbuf_server_init:
 * @isb: is a pointer to a #gnutls_sbuf_t structure.
 * @fd: a socket descriptor
 * @priority: A priority string to use (use %NULL for default)
 * @cred: A credentials structure
 * @flags: should be zero or %GNUTLS_SBUF_WRITE_FLUSHES
 *
 * This function initializes a #gnutls_sbuf_t structure.
 * If the flag %GNUTLS_SBUF_WRITE_FLUSHES
 * is set then gnutls_sbuf_queue() will flush when the maximum
 * data size for a record is reached.
 *
 * Returns: %GNUTLS_E_SUCCESS on success, or an error code.
 *
 * Since: 3.1.7
 **/
int gnutls_sbuf_server_init (gnutls_sbuf_t * isb,
                             gnutls_transport_ptr fd, 
                             const char* priority, gnutls_credentials_t cred,
                             unsigned int flags)
{
struct gnutls_sbuf_st* sb;
gnutls_session_t session;
int ret;

  ret = gnutls_init(&session, GNUTLS_SERVER);
  if (ret < 0)
    return gnutls_assert_val(ret);

  sb = gnutls_calloc(1, sizeof(*sb));
  if (sb == NULL)
    {
      ret = gnutls_assert_val(GNUTLS_E_MEMORY_ERROR);
      goto fail1;
    }
  _gnutls_buffer_init(&sb->buf);
  sb->session = session;
  sb->flags = flags;
  
  /* set session/handshake info 
   */
  gnutls_handshake_set_timeout(session, GNUTLS_DEFAULT_HANDSHAKE_TIMEOUT);
  
  if (priority == NULL) priority = "NORMAL:%COMPAT";
  ret = gnutls_priority_set_direct(session, priority, NULL);
  if (ret < 0)
    {
      gnutls_assert();
      goto fail1;
    }
  
  if (cred->xcred)
    {
      ret = gnutls_credentials_set(session, GNUTLS_CRD_CERTIFICATE, cred->xcred);
      if (ret < 0)
        {
          gnutls_assert();
          goto fail1;
        }
    }

  gnutls_transport_set_ptr (session, fd);
  gnutls_session_set_ptr( session, sb);

  do
    {
      ret = gnutls_handshake(session);
    }
  while (ret < 0 && gnutls_error_is_fatal (ret) == 0);

  if (ret < 0)
    {
      gnutls_assert();
      goto fail1;
    }
  
  *isb = sb;
  
  return 0;

fail1:
  if (sb)
    gnutls_sbuf_deinit(sb);
  gnutls_deinit(session);
  
  return ret;
}

/**
 * gnutls_sbuf_deinit:
 * @sb: is a #gnutls_sbuf_t structure.
 *
 * This function clears all buffers associated with the @sb
 * structure. The GnuTLS session associated with the structure
 * is left intact.
 *
 * Since: 3.1.7
 **/
void gnutls_sbuf_deinit(gnutls_sbuf_t sb)
{
  if (sb->session)
    {
      gnutls_bye(sb->session, GNUTLS_SHUT_WR);
      gnutls_deinit(sb->session);
    }
  _gnutls_buffer_clear(&sb->buf);
  gnutls_free(sb);
}

/**
 * gnutls_sbuf_write:
 * @sb: is a #gnutls_sbuf_t structure.
 * @data: contains the data to send
 * @data_size: is the length of the data
 *
 * This function is the buffered equivalent of gnutls_record_send().
 * Instead of sending the data immediately the data are buffered
 * until gnutls_sbuf_queue() is called, or if the flag %GNUTLS_SBUF_WRITE_FLUSHES
 * is set, until the number of bytes for a full record is reached.
 *
 * This function must only be used with blocking sockets.
 *
 * Returns: On success, the number of bytes written is returned, otherwise
 *  an error code is returned.
 *
 * Since: 3.1.7
 **/
ssize_t gnutls_sbuf_write (gnutls_sbuf_t sb, const void *data,
                           size_t data_size)
{
int ret;
  
  ret = _gnutls_buffer_append_data(&sb->buf, data, data_size);
  if (ret < 0)
    return gnutls_assert_val(ret);
  
  while ((sb->flags & GNUTLS_SBUF_WRITE_FLUSHES) && 
       sb->buf.length >= MAX_RECORD_SEND_SIZE(sb->session))
    {
      do
        {
          ret = gnutls_record_send(sb->session, sb->buf.data, sb->buf.length);
        }
      while (ret < 0 && gnutls_error_is_fatal(ret) == 0);
      if (ret < 0)
        return gnutls_assert_val(ret);

      sb->buf.data += ret;
      sb->buf.length -= ret;
    }
  
  return data_size;
}

/**
 * gnutls_sbuf_printf:
 * @sb: is a #gnutls_sbuf_t structure.
 * @fmt: printf-style format 
 *
 * This function allows writing to a %gnutls_sbuf_t using printf
 * style arguments.
 *
 * This function must only be used with blocking sockets.
 *
 * Returns: On success, the number of bytes written is returned, otherwise
 *  an error code is returned.
 *
 * Since: 3.1.7
 **/
ssize_t gnutls_sbuf_printf (gnutls_sbuf_t sb, const char *fmt, ...)
{
int ret;
va_list args;
int len;
char* str;

  va_start(args, fmt);
  len = vasprintf(&str, fmt, args);
  va_end(args);
  
  if (len < 0 || !str)
    return gnutls_assert_val(GNUTLS_E_MEMORY_ERROR);
  
  ret = gnutls_sbuf_write (sb, str, len);
  
  gnutls_free(str);

  return ret;
}

/**
 * gnutls_sbuf_flush:
 * @sb: is a #gnutls_sbuf_t structure.
 *
 * This function flushes the buffer @sb. All the data stored are transmitted.
 *
 * This function must only be used with blocking sockets.
 *
 * Returns: On success, the number of bytes sent, otherwise a negative error code.
 *
 * Since: 3.1.7
 **/
ssize_t gnutls_sbuf_flush (gnutls_sbuf_t sb)
{
int ret;
ssize_t total = 0;

  while(sb->buf.length > 0)
    {
      do
        {
          ret = gnutls_record_send(sb->session, sb->buf.data, sb->buf.length);
        }
      while (ret < 0 && gnutls_error_is_fatal(ret) == 0);
      if (ret < 0)
        return gnutls_assert_val(ret);

      sb->buf.data += ret;
      sb->buf.length -= ret;
      total += ret;
    }

  return total;
}

/**
 * gnutls_sbuf_handshake:
 * @sb: is a #gnutls_sbuf_t structure.
 *
 * This function performs a handshake on the underlying session.
 * Only fatal errors are returned by this function.
 *
 * Returns: On success, zero is returned, otherwise a negative error code.
 *
 * Since: 3.1.7
 **/
int gnutls_sbuf_handshake(gnutls_sbuf_t sb)
{
int ret, ret2;

  do
    {
      ret = gnutls_handshake(sb->session);
    }
  while (ret < 0 && gnutls_error_is_fatal(ret) == 0);
  if (ret < 0)
    {
      do
        {
          ret2 = gnutls_alert_send_appropriate(sb->session, ret);
        }
      while (ret2 < 0 && gnutls_error_is_fatal(ret2) == 0);

      return gnutls_assert_val(ret);
    }
    
  return 0;
}

/**
 * gnutls_sbuf_read:
 * @sb: is a #gnutls_sbuf_t structure.
 * @data: the buffer that the data will be read into
 * @data_size: the number of requested bytes
 *
 * This function receives data from the underlying session.
 * Only fatal errors are returned by this function.
 *
 * Returns: The number of bytes received and zero on EOF (for stream
 * connections) or a negative error code.
 *
 * Since: 3.1.7
 **/
ssize_t gnutls_sbuf_read(gnutls_sbuf_t sb, void* data, size_t data_size)
{
int ret;

  do
    {
      ret = gnutls_record_recv(sb->session, data, data_size);
    }
  while (ret < 0 && gnutls_error_is_fatal(ret) == 0);

  if (ret < 0)
    return gnutls_assert_val(ret);

  return 0;
}

/**
 * gnutls_sbuf_get_session:
 * @sb: is a #gnutls_sbuf_t structure.
 *
 * Returns: The associated session or %NULL.
 *
 * Since: 3.1.7
 **/
gnutls_session_t gnutls_sbuf_get_session(gnutls_sbuf_t sb)
{
  return sb->session;
}
