#ifndef UVHTTP_SSL_CLIENT_H__
#define UVHTTP_SSL_CLIENT_H__
#include <stdlib.h>
#if defined(_MSC_VER) || defined(__MINGW64_VERSION_MAJOR)
#include <crtdbg.h>
#endif
#include <uv.h>
#include "mbedtls/config.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "uvhttp_internal.h"
#include "uvhttp_util.h"

struct uvhttp_ssl_client {
    uv_tcp_t tcp;
    uv_buf_t user_read_buf;
    uv_close_cb user_close_cb;
    uv_read_cb user_read_cb;
    uv_write_cb user_write_cb;
    uv_alloc_cb user_alloc_cb;
    uv_write_t* user_write_req;
    char ssl_read_buffer[UVHTTP_NET_BUFFER_SIZE];
    unsigned int ssl_read_buffer_len;
    unsigned int ssl_read_buffer_offset;
    unsigned int ssl_write_offset;
    unsigned int ssl_write_buffer_len;
    char* ssl_write_buffer;
    char is_async_writing;
    char is_writing;
    char is_write_error;
    char is_closing;
    uv_buf_t write_buffer;
    mbedtls_ssl_context ssl;
    mbedtls_pk_context key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt srvcert;
};

int uvhttp_ssl_client_init(
    uv_loop_t* loop,
    uv_tcp_t* handle
    );

int uvhttp_ssl_client_connect(
    uv_connect_t* req,
    uv_tcp_t* handle,
    const struct sockaddr* addr,
    uv_connect_cb cb
    );

int uvhttp_ssl_read_client_start(
    uv_stream_t* stream,
    uv_alloc_cb alloc_cb,
    uv_read_cb read_cb
    );

int uvhttp_ssl_client_write(
    uv_write_t* req,
    uv_stream_t* handle,
    char* buffer,
    unsigned int buffer_len,
    uv_write_cb cb
    );

void uvhttp_ssl_client_close(
    uv_handle_t* handle, 
    uv_close_cb close_cb
    );

#endif // UVHTTP_SSL_CLIENT_H__
