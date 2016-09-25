#include "uvhttp_ssl_server.h"
#include "mbedtls/certs.h"
#include "mbedtls/debug.h"
#include "uvhttp_base.h"

static void uvhttp_ssl_session_close_cb(
    uv_handle_t* handle
    );

static void uvhttp_ssl_read_cb(
    uv_stream_t* stream,
    ssize_t nread,
    const uv_buf_t* buf
    )
{
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)stream;
    if ( nread <= 0) {
        goto cleanup;
    }
    if ( ssl->is_closing) {
        int ret = mbedtls_ssl_close_notify( &ssl->ssl);
        if ( uv_is_closing( (uv_handle_t*)ssl) == 0) {
            uv_close( (uv_handle_t*)stream, uvhttp_ssl_session_close_cb);
        }
    }
    else if ( ssl->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER) {
        int ret = 0;
        ssl->ssl_read_buffer_len = nread;
        ssl->ssl_read_buffer_offset = 0;
        while ( (ret = mbedtls_ssl_handshake_step( &ssl->ssl )) == 0) {
            if ( ssl->ssl.state == MBEDTLS_SSL_HANDSHAKE_OVER){
                break;
            }
        }
        if ( ssl->ssl_read_buffer_offset != nread) {
            nread = -1;
            goto cleanup;
        }
        ssl->ssl_read_buffer_len = 0;
        ssl->ssl_read_buffer_offset = 0;
        if ( ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            nread = -1;
            goto cleanup;
        }
    }
    else {
        int ret = 0;
        int read_len = 0;
        ssl->ssl_read_buffer_len = nread;
        ssl->ssl_read_buffer_offset = 0;

        //���ܱ�������û�ж�ȡ��һ�������Ŀ飬����һ������Ҳ�����ء�
        while((read_len = mbedtls_ssl_read( &ssl->ssl, 
            (unsigned char *)ssl->user_read_buf.base,  ssl->user_read_buf.len)) > 0) {
            ssl->user_read_cb( stream, read_len, &ssl->user_read_buf);
        }
        if ( read_len !=0 && read_len != MBEDTLS_ERR_SSL_WANT_READ) {
            if ( MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY == read_len) {
                nread = UV_ECONNABORTED;
                goto cleanup;
            }
            else {
                nread = -1;
                goto cleanup;
            }
        }
    }
cleanup:
    if ( nread <= 0) {
        ssl->user_read_cb( stream, nread, 0);
    }
}

static void uvhttp_ssl_alloc_cb(
    uv_handle_t* handle,
    size_t suggested_size,
    uv_buf_t* buf
    )
{
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)handle;
    ssl->user_read_buf.base = 0;
    ssl->user_read_buf.len = 0;
    ssl->user_alloc_cb( handle, suggested_size, &ssl->user_read_buf);
    buf->base = ssl->ssl_read_buffer;
    buf->len = sizeof( ssl->ssl_read_buffer);
}

int uvhttp_ssl_listen( 
    uv_stream_t* stream,
    int backlog, 
    uv_connection_cb cb
    )
{
    int ret = 0;

    return ret;
}

static int ssl_recv( 
    void *ctx, 
    unsigned char *buf,
    size_t len
    )
{
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)ctx;
    int need_copy = min( ssl->ssl_read_buffer_len - ssl->ssl_read_buffer_offset, (int)len);
    if ( need_copy == 0) {
        need_copy = MBEDTLS_ERR_SSL_WANT_READ;
        goto cleanup;
    }
    memcpy( buf, ssl->ssl_read_buffer + ssl->ssl_read_buffer_offset, need_copy);
    ssl->ssl_read_buffer_offset += need_copy;
cleanup:
    return need_copy ;
}

static void ssl_write_user_call( 
    struct uvhttp_ssl_session* ssl,
    int status
    )
{
    ssl->is_writing = 0;
    if ( ssl->user_write_cb) {
        ssl->user_write_cb( ssl->user_write_req,  status);
    }
}

static void ssl_write_cb(
    uv_write_t* req,
    int status
    )
{
    int ret = 0;
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)req->data;
    if( status != 0) {
        ret = UVHTTP_ERROR_FAILED;
        goto cleanup;
    }
    if ( ssl->is_write_error ) {
        ret = UVHTTP_ERROR_FAILED;
        goto cleanup;
    }
    if ( ssl->write_buffer.base) {
        free( ssl->write_buffer.base);
        ssl->write_buffer.base = 0;
    }
    //����״̬
    if ( ssl->is_closing) {
        int ret = mbedtls_ssl_close_notify( &ssl->ssl);
        if ( uv_is_closing( (uv_handle_t*)ssl) == 0) {
            uv_close( (uv_handle_t*)ssl, uvhttp_ssl_session_close_cb);
        }
    }
    else if ( ssl->ssl.state != MBEDTLS_SSL_HANDSHAKE_OVER ) {
		while ( (ret = mbedtls_ssl_handshake_step( &ssl->ssl )) == 0) {
			if ( ssl->ssl.state == MBEDTLS_SSL_HANDSHAKE_OVER){
				break;
			}
		}
		if ( ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
            ret = UVHTTP_ERROR_FAILED;
			goto cleanup;
		}
        ret = 0;
    }
    else {
    //����״̬
        //��Ҫ�޸Ğ�ʹ��whileѭ�h
        ret = mbedtls_ssl_write( &ssl->ssl,
            (const unsigned char *)ssl->ssl_write_buffer + ssl->ssl_write_offset, 
            ssl->ssl_write_buffer_len - ssl->ssl_write_offset
            );
        if ( ret == MBEDTLS_ERR_SSL_WANT_WRITE ) {
            ret = UVHTTP_ERROR_FAILED;
            goto cleanup;
        }
        else if ( ret > 0 ) {
            //д����һ��buffer
            ssl->ssl_write_offset += ret;
            if ( ssl->ssl_write_offset == ssl->ssl_write_buffer_len) {
                //д����ɻص�
                ssl_write_user_call( ssl,  0);
            }
            ret = 0;
        }
        else {
            ret = UVHTTP_ERROR_FAILED;
            goto cleanup;
        }
    }
cleanup:
    if( ret != 0) {
        if ( !ssl->is_write_error ) {
            ssl_write_user_call( ssl, UVHTTP_ERROR_FAILED);
            ssl->is_write_error = 1;
        }
    }
    if ( ssl->write_buffer.base) {
        free( ssl->write_buffer.base);
        ssl->write_buffer.base = 0;
    }
    if ( req) {
        free( req);
    }
}

static int ssl_send(
    void *ctx, 
    const unsigned char *buf, 
    size_t len
    )
{
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)ctx;
    uv_write_t* write_req = 0;
    int ret = 0;
    ssl->write_buffer.base = 0;
    ssl->write_buffer.len = 0;
    if ( ssl->is_write_error ) {
        return UVHTTP_ERROR_FAILED;
    }
    if ( ssl->is_async_writing == 0 ) {
        ssl->write_buffer.base = (char*)malloc( len );
        memcpy( ssl->write_buffer.base, buf, len);
        ssl->write_buffer.len = len;
        write_req = (uv_write_t*)malloc(sizeof(uv_write_t));
        write_req->data = ssl;
        ret = uv_write( write_req, (uv_stream_t*)ssl, &ssl->write_buffer, 1, ssl_write_cb);
        if ( ret != 0) {
            goto cleanup;
        }
        len = MBEDTLS_ERR_SSL_WANT_WRITE;
        ssl->is_async_writing = 1;
    }
    else {
        ssl->is_async_writing = 0;
    }
cleanup:
    if ( ret != 0) {
        len = UVHTTP_ERROR_FAILED;
        ssl->is_write_error = 1;
        ssl_write_user_call( ssl, UVHTTP_ERROR_FAILED);
        if ( write_req) {
            free( write_req);
        }
        if ( ssl->write_buffer.base) {
            free( ssl->write_buffer.base);
            ssl->write_buffer.base = 0;
        }
    }
    return len;
}

static void my_debug( void *ctx, int level,
	const char *file, int line,
	const char *str )
{
	const char *p, *basename;

	/* Extract basename from file */
	for( p = basename = file; *p != '\0'; p++ )
		if( *p == '/' || *p == '\\' )
			basename = p + 1;

	mbedtls_fprintf( (FILE *) ctx, "%s:%04d: |%d| %s", basename, line, level, str );
	fflush(  (FILE *) ctx  );
}

int uvhttp_ssl_server_init(
    uv_loop_t* loop, 
    uv_tcp_t* handle
    )
{
    int ret = 0;
    struct uvhttp_ssl_server* ssl = (struct uvhttp_ssl_server*)handle;

    mbedtls_ssl_config_init( &ssl->conf );
    mbedtls_x509_crt_init( &ssl->srvcert );
    mbedtls_ctr_drbg_init( &ssl->ctr_drbg );
    mbedtls_entropy_init( &ssl->entropy );
    mbedtls_pk_init( &ssl->key );

    if( ( ret = mbedtls_ctr_drbg_seed( &ssl->ctr_drbg, mbedtls_entropy_func, &ssl->entropy,
        (const unsigned char *) "UVHTTP",
        sizeof( "UVHTTP" ) -1) ) != 0 ) {
            goto cleanup;
    }

    ret = mbedtls_x509_crt_parse( &ssl->srvcert, (const unsigned char *) mbedtls_test_srv_crt,
        mbedtls_test_srv_crt_len );
    if( ret < 0 ) {
        goto cleanup;
    }

    ret = mbedtls_x509_crt_parse( &ssl->srvcert, (const unsigned char *) mbedtls_test_cas_pem,
        mbedtls_test_cas_pem_len );
    if( ret != 0 ) {
        goto cleanup;
    }

    ret =  mbedtls_pk_parse_key( &ssl->key, (const unsigned char *) mbedtls_test_srv_key,
        mbedtls_test_srv_key_len, NULL, 0 );
    if( ret < 0 ) {
        goto cleanup;
    }

    if( ( ret = mbedtls_ssl_config_defaults( &ssl->conf,
        MBEDTLS_SSL_IS_SERVER,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 ) {
            goto cleanup;
    }

    mbedtls_ssl_conf_authmode( &ssl->conf, MBEDTLS_SSL_VERIFY_OPTIONAL );
    mbedtls_ssl_conf_ca_chain( &ssl->conf, &ssl->srvcert, NULL );
    if( ( ret = mbedtls_ssl_conf_own_cert( &ssl->conf, &ssl->srvcert, &ssl->key) ) != 0 ) {
        goto cleanup;
    }
    mbedtls_ssl_conf_rng( &ssl->conf, mbedtls_ctr_drbg_random, &ssl->ctr_drbg );

    //mbedtls_ssl_conf_dbg( &ssl->conf, my_debug, stdout );
    //mbedtls_debug_set_threshold( 1 );

    ret = uv_tcp_init( loop, (uv_tcp_t*)&ssl->tcp);
    if ( ret != 0) {
        goto cleanup;
    }
cleanup:
    return ret;
}

int uvhttp_ssl_session_init(
    uv_loop_t* loop,
    uv_tcp_t* handle,
    uv_tcp_t* server
    )
{
    int ret = 0;
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)handle;
    struct uvhttp_ssl_server* ssl_server = (struct uvhttp_ssl_server*)server;

    mbedtls_ssl_init( &ssl->ssl );

    if( ( ret = mbedtls_ssl_setup( &ssl->ssl, &ssl_server->conf ) ) != 0 ) {
        goto cleanup;
    }
    mbedtls_ssl_set_bio( &ssl->ssl, ssl, ssl_send, ssl_recv, NULL );

    ret = uv_tcp_init( loop, (uv_tcp_t*)&ssl->tcp);
    if ( ret != 0) {
        goto cleanup;
    }
cleanup:

    return ret;
}

int uvhttp_ssl_read_session_start(
    uv_stream_t* stream,
    uv_alloc_cb alloc_cb,
    uv_read_cb read_cb
    )
{
    int ret = 0;
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)stream;
    ssl->user_read_cb = read_cb;
    ssl->user_alloc_cb = alloc_cb;
    ret = uv_read_start( stream, uvhttp_ssl_alloc_cb, uvhttp_ssl_read_cb);
    return ret;
}

int uvhttp_ssl_session_write(
    uv_write_t* req,
    uv_stream_t* handle,
    char* buffer,
    unsigned int buffer_len,
    uv_write_cb cb
    )
{
    int ret = 0;
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)handle;
    if ( ssl->is_writing || ssl->is_closing ) {
        ret = UVHTTP_ERROR_WRITE_WAIT;
        goto cleanup;
    }
    ssl->is_write_error = 0;
    ssl->user_write_req = req;
    ssl->user_write_cb = cb;
    ssl->ssl_write_buffer_len = buffer_len;
    ssl->ssl_write_buffer = buffer;
    ssl->ssl_read_buffer_len = 0;
    ssl->ssl_read_buffer_offset = 0;
    ssl->ssl_write_offset = 0;
    ssl->is_async_writing = 0;
    if ( mbedtls_ssl_write( &ssl->ssl, (const unsigned char *)buffer, 
        buffer_len ) == MBEDTLS_ERR_SSL_WANT_WRITE ) {
        ssl->is_writing = 1;
    }
cleanup:
    return ret;
}

static void uvhttp_ssl_session_close_cb(
    uv_handle_t* handle
    )
{
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)handle;
    mbedtls_ssl_free( &ssl->ssl );
    ssl->user_close_cb( handle);
}

void uvhttp_ssl_session_close(
    uv_handle_t* handle, 
    uv_close_cb close_cb
    )
{
    struct uvhttp_ssl_session* ssl = (struct uvhttp_ssl_session*)handle;
    int ret = 0;
    ret = mbedtls_ssl_close_notify( &ssl->ssl);
    ssl->is_closing = 1;
    ((struct uvhttp_ssl_session*)handle)->user_close_cb = close_cb;
    if ( MBEDTLS_ERR_SSL_WANT_READ != ret && MBEDTLS_ERR_SSL_WANT_WRITE != ret ) {
        if ( uv_is_closing( (uv_handle_t*)ssl) == 0) {
            uv_close( handle, uvhttp_ssl_session_close_cb);
        }
    }
}

static void uvhttp_ssl_server_close_cb(
    uv_handle_t* handle
    )
{
    struct uvhttp_ssl_server* ssl = (struct uvhttp_ssl_server*)handle;
    mbedtls_x509_crt_free( &ssl->srvcert );
    mbedtls_pk_free( &ssl->key );
    mbedtls_ssl_config_free( &ssl->conf );
    mbedtls_ctr_drbg_free( &ssl->ctr_drbg );
    mbedtls_entropy_free( &ssl->entropy );
    ssl->user_close_cb( handle);
}

void uvhttp_ssl_server_close(
    uv_handle_t* handle, 
    uv_close_cb close_cb
    )
{
    ((struct uvhttp_ssl_server*)handle)->user_close_cb = close_cb;
    if ( uv_is_closing( handle) == 0) {
        uv_close( handle, uvhttp_ssl_server_close_cb);
    }
}