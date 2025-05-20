/* Copyright (c) 2017 - 2022 LiteSpeed Technologies Inc.  See LICENSE. */
/*
 * md5_server.c -- Read one or more streams from the client and return
 *                 MD5 sum of the payload.
 */

#include <zlib.h>
#include <brotli/decode.h>
#include <zstd.h>
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <time.h>
#include <unistd.h>

#include <openssl/md5.h>

#include <event2/event.h>

#include "lsquic.h"
#include "test_common.h"
#include "../src/liblsquic/lsquic_hash.h"
#include "test_cert.h"
#include "prog.h"

#include "../src/liblsquic/lsquic_logger.h"

#define DATETIME_SIZE 24
char *compression_type = NULL;
int compress_flag = 0;
static int save_to_file = 1;
static int g_really_calculate_md5 = 0;

void get_datetime_with_milliseconds();

/* Turn on to test whether stream reset is being sent when stream is closed
 * prematurely.
 */
static struct {
    unsigned        stream_id;
    unsigned long   limit;
    unsigned long   n_read;
} g_premature_close;

struct lsquic_conn_ctx;

struct server_ctx {
    TAILQ_HEAD(, lsquic_conn_ctx)   conn_ctxs;
    unsigned max_reqs;
    int n_conn;
    time_t expiry;
    struct sport_head sports;
    struct prog *prog;
};

struct lsquic_conn_ctx {
    TAILQ_ENTRY(lsquic_conn_ctx)    next_connh;
    lsquic_conn_t       *conn;
    unsigned             n_reqs, n_closed;
    struct server_ctx   *server_ctx;
};


static lsquic_conn_ctx_t *
server_on_new_conn (void *stream_if_ctx, lsquic_conn_t *conn)
{
    struct server_ctx *server_ctx = stream_if_ctx;
    lsquic_conn_ctx_t *conn_h = calloc(1, sizeof(*conn_h));
    conn_h->conn = conn;
    conn_h->server_ctx = server_ctx;
    TAILQ_INSERT_TAIL(&server_ctx->conn_ctxs, conn_h, next_connh);
    LSQ_NOTICE("New connection!");
    print_conn_info(conn);
    return conn_h;
}


static void
server_on_conn_closed (lsquic_conn_t *conn)
{
    lsquic_conn_ctx_t *conn_h = lsquic_conn_get_ctx(conn);
    int stopped;

    if (conn_h->server_ctx->expiry && conn_h->server_ctx->expiry < time(NULL))
    {
        LSQ_NOTICE("reached engine expiration time, shut down");
        prog_stop(conn_h->server_ctx->prog);
        stopped = 1;
    }
    else
        stopped = 0;

    if (conn_h->server_ctx->n_conn)
    {
        --conn_h->server_ctx->n_conn;
        LSQ_NOTICE("Connection closed, remaining: %d", conn_h->server_ctx->n_conn);
        if (0 == conn_h->server_ctx->n_conn && !stopped)
            prog_stop(conn_h->server_ctx->prog);
    }
    else
        LSQ_NOTICE("Connection closed");
    TAILQ_REMOVE(&conn_h->server_ctx->conn_ctxs, conn_h, next_connh);

    lsquic_conn_set_ctx(conn, NULL);
    free(conn_h);

    get_datetime_with_milliseconds();
    //prog_cleanup(&conn_h->server_ctx->prog);
    exit(EXIT_SUCCESS);

}


void get_datetime_with_milliseconds() {
    char datetime_string[DATETIME_SIZE];
    // Get the current time
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Convert time to struct tm for formatting
    time_t rawtime = tv.tv_sec;
    struct tm *tm_info = localtime(&rawtime);

    // Format the time as a string with milliseconds
    strftime(datetime_string, DATETIME_SIZE, "%Y-%m-%d %H:%M:%S", tm_info);

    // Append milliseconds to the datetime string
    sprintf(datetime_string + strlen(datetime_string), ".%03d", (int)tv.tv_usec / 1000);
    printf("|%s", datetime_string);
}


struct lsquic_stream_ctx {
    lsquic_stream_t     *stream;
    struct server_ctx   *server_ctx;
    MD5_CTX              md5ctx;
    unsigned char        md5sum[MD5_DIGEST_LENGTH];
    char                 md5str[MD5_DIGEST_LENGTH * 2 + 1];
    FILE                *file;
    z_stream             zstream; // added
    BrotliDecoderState  *brotli_state; // added
    ZSTD_DCtx           *zstd_dctx;
    char                *buffer;  // Buffer to store the data
    size_t               buflen;  // Length of the buffer
};



static struct lsquic_conn_ctx *
find_conn_h (const struct server_ctx *server_ctx, lsquic_stream_t *stream)
{
    struct lsquic_conn_ctx *conn_h;
    lsquic_conn_t *conn;

    conn = lsquic_stream_conn(stream);
    TAILQ_FOREACH(conn_h, &server_ctx->conn_ctxs, next_connh)
        if (conn_h->conn == conn)
            return conn_h;
    return NULL;
}


// for each stream
static lsquic_stream_ctx_t *
server_md5_on_new_stream (void *stream_if_ctx, lsquic_stream_t *stream)
{




    struct lsquic_conn_ctx *conn_h;
    lsquic_stream_ctx_t *st_h = malloc(sizeof(*st_h));
    st_h->stream = stream;
    st_h->server_ctx = stream_if_ctx;
    lsquic_stream_wantread(stream, 1);
    if (g_really_calculate_md5)
        MD5_Init(&st_h->md5ctx);
    conn_h = find_conn_h(st_h->server_ctx, stream);
    assert(conn_h);
    conn_h->n_reqs++;
    LSQ_NOTICE("request #%u", conn_h->n_reqs);


    // Initialize the zlib stream
    st_h->zstream.zalloc = Z_NULL;
    st_h->zstream.zfree = Z_NULL;
    st_h->zstream.opaque = Z_NULL;
    st_h->zstream.avail_in = 0;
    st_h->zstream.next_in = Z_NULL;
    inflateInit(&st_h->zstream);
    st_h->brotli_state = BrotliDecoderCreateInstance(NULL, NULL, NULL);
    st_h->zstd_dctx     = ZSTD_createDCtx();
    // Initialize the buffer
    st_h->buffer = NULL;
    st_h->buflen = 0;



    // creating file
    if (save_to_file) {
        //printf("creating file\n");
        char filename[256];
        sprintf(filename, "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/received/file_%ld", lsquic_stream_id(stream));
        st_h->file = fopen(filename, "wb");
        if (!st_h->file) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
    }




    if (st_h->server_ctx->max_reqs &&
        conn_h->n_reqs >= st_h->server_ctx->max_reqs)
    {
        /* The assert guards the assumption that after the we mark the
         * connection as going away, no new streams are opened and thus
         * this callback is not called.
         */
        assert(conn_h->n_reqs == st_h->server_ctx->max_reqs);
        LSQ_NOTICE("reached maximum requests: %u, going away",
            st_h->server_ctx->max_reqs);
        lsquic_conn_going_away(conn_h->conn);
    }
    return st_h;
}


// opens up for each 4096 bytes
static void
server_md5_on_read (lsquic_stream_t *stream, lsquic_stream_ctx_t *st_h)
{
    char buf[0x1000];
    ssize_t nr;

    nr = lsquic_stream_read(stream, buf, sizeof(buf));
    if (-1 == nr)
    {
        /* This should never return an error if we only call read() once
         * per callback.
         */
        perror("lsquic_stream_read");
        lsquic_stream_shutdown(stream, 0);
        return;
    }

    // Append the received data to the buffer
    st_h->buffer = realloc(st_h->buffer, st_h->buflen + nr);
    memcpy(st_h->buffer + st_h->buflen, buf, nr);
    st_h->buflen += nr;


    // Write the received data to the file
    if (save_to_file) {
        //fwrite(buf, 1, nr, st_h->file);
    }



    if (g_premature_close.limit &&
                g_premature_close.stream_id == lsquic_stream_id(stream))
    {
        g_premature_close.n_read += nr;
        if (g_premature_close.n_read > g_premature_close.limit)
        {
            LSQ_WARN("Done after reading %lu bytes", g_premature_close.n_read);
            lsquic_stream_shutdown(stream, 0);
            return;
        }
    }

    if (nr)
    {
        if (g_really_calculate_md5)
            MD5_Update(&st_h->md5ctx, buf, nr);
    }
    else
    {
        lsquic_stream_wantread(stream, 0);
        if (g_really_calculate_md5)
        {
            MD5_Final(st_h->md5sum, &st_h->md5ctx);
            snprintf(st_h->md5str, sizeof(st_h->md5str),
                "%02x%02x%02x%02x%02x%02x%02x%02x"
                "%02x%02x%02x%02x%02x%02x%02x%02x"
                , st_h->md5sum[0]
                , st_h->md5sum[1]
                , st_h->md5sum[2]
                , st_h->md5sum[3]
                , st_h->md5sum[4]
                , st_h->md5sum[5]
                , st_h->md5sum[6]
                , st_h->md5sum[7]
                , st_h->md5sum[8]
                , st_h->md5sum[9]
                , st_h->md5sum[10]
                , st_h->md5sum[11]
                , st_h->md5sum[12]
                , st_h->md5sum[13]
                , st_h->md5sum[14]
                , st_h->md5sum[15]
            );
        }
        else
        {
            memset(st_h->md5str, '0', 32);
        }

        lsquic_stream_wantwrite(stream, 1);
        lsquic_stream_shutdown(stream, 0);
    }

}


static void
server_md5_on_write (lsquic_stream_t *stream, lsquic_stream_ctx_t *st_h)
{
    ssize_t nw;
    nw = lsquic_stream_write(stream, st_h->md5str, sizeof(st_h->md5str) - 1);
    if (-1 == nw)
    {
        perror("lsquic_stream_write");
        return;
    }
    lsquic_stream_wantwrite(stream, 0);
    lsquic_stream_shutdown(stream, 1);
}


// is called from each stream when it is going to be closed
static void
server_on_close (lsquic_stream_t *stream, lsquic_stream_ctx_t *st_h)
{
    struct lsquic_conn_ctx *conn_h;
    LSQ_NOTICE("%s called", __func__);
    conn_h = find_conn_h(st_h->server_ctx, stream);
    conn_h->n_closed++;
    if (st_h->server_ctx->max_reqs &&
        conn_h->n_closed >= st_h->server_ctx->max_reqs)
    {
        assert(conn_h->n_closed == st_h->server_ctx->max_reqs);
        LSQ_NOTICE("closing connection after completing %u requests",
            conn_h->n_closed);
        lsquic_conn_close(conn_h->conn);
    }

    //printf("size of buf %ld\n", st_h->buflen);


    // decompression
    if (compress_flag) {
        
        // Decompress the data from the buffer
        if (strcmp(compression_type, "zlib") == 0){
            char outbuf[0x1000];
            int ret;
            st_h->zstream.avail_in = st_h->buflen;
            st_h->zstream.next_in = (Bytef *)st_h->buffer;
            do {
                st_h->zstream.avail_out = sizeof(outbuf);
                st_h->zstream.next_out = (Bytef *)outbuf;
                ret = inflate(&st_h->zstream, Z_FINISH);
                assert(ret != Z_STREAM_ERROR);
                switch (ret) {
                    case Z_NEED_DICT:
                        ret = Z_DATA_ERROR;
                    case Z_DATA_ERROR:
                    case Z_MEM_ERROR:
                        inflateEnd(&st_h->zstream);
                        return;
                }
                size_t have = sizeof(outbuf) - st_h->zstream.avail_out;
    
                if (save_to_file) {
                    fwrite(outbuf, 1, have, st_h->file);
                }
            } while (st_h->zstream.avail_out == 0);
    
            // Close the zlib stream
            inflateEnd(&st_h->zstream);
        } else if (strcmp(compression_type, "brotli") == 0) {
            char outbuf[0x1000];
            size_t start_pos = 0;
            while (start_pos < st_h->buflen){
                size_t available_input = st_h->buflen - start_pos;
                const uint8_t *next_pos = (uint8_t *) st_h->buffer + start_pos;
                uint8_t *next_out = outbuf;
                size_t output_size = sizeof(outbuf);

                BrotliDecoderResult result = BrotliDecoderDecompressStream(st_h->brotli_state, &available_input, &next_pos, &output_size, &next_out, NULL);
                start_pos = st_h->buflen - available_input;
                size_t current_size = sizeof(outbuf) - output_size;

                if (save_to_file && current_size){
                    fwrite(outbuf, 1 , current_size, st_h->file);
                }
                
                if (result == (BROTLI_DECODER_RESULT_ERROR || BROTLI_DECODER_RESULT_SUCCESS))
                   break;
                
            }
            BrotliDecoderDestroyInstance(st_h->brotli_state);
        }
        else if (strcmp(compression_type, "zstd") == 0) {
            char outbuf[0x1000];
            ZSTD_inBuffer input = {st_h->buffer, st_h->buflen, 0};
            ZSTD_outBuffer output = {outbuf, sizeof(outbuf), 0};
            while (input.pos < input.size){
                size_t result = ZSTD_decompressStream(st_h->zstd_dctx, &output, &input);

                if (ZSTD_isError(result))
                    break;
                
                if (save_to_file && output.pos){
                    fwrite(outbuf, 1 , output.pos, st_h->file);
                    output.pos = 0;
                }
                
            }
            ZSTD_freeDCtx(st_h->zstd_dctx);
            

        }        

    } else if (save_to_file) {
        fwrite(st_h->buffer, 1, st_h->buflen, st_h->file);
    }



    // Free the buffer
    free(st_h->buffer);


    // closing file
    if (save_to_file) {
        fclose(st_h->file);
    }

    free(st_h);
}








const struct lsquic_stream_if server_md5_stream_if = {
    .on_new_conn            = server_on_new_conn,
    .on_conn_closed         = server_on_conn_closed,
    .on_new_stream          = server_md5_on_new_stream,
    .on_read                = server_md5_on_read,
    .on_write               = server_md5_on_write,
    .on_close               = server_on_close,
};


static void
usage (const char *prog)
{
    const char *const slash = strrchr(prog, '/');
    if (slash)
        prog = slash + 1;
    printf(
"Usage: %s [opts]\n"
"\n"
"Options:\n"
"   -e EXPIRY   Stop engine after this many seconds.  The expiration is\n"
"                 checked when connections are closed.\n"
                , prog);
}


int
main (int argc, char **argv)
{
    LSQ_NOTICE("Starting server\n");
    int opt, s;
    struct prog prog;
    struct server_ctx server_ctx;

    memset(&server_ctx, 0, sizeof(server_ctx));
    TAILQ_INIT(&server_ctx.conn_ctxs);
    server_ctx.prog = &prog;
    TAILQ_INIT(&server_ctx.sports);
    prog_init(&prog, LSENG_SERVER, &server_ctx.sports,
                                    &server_md5_stream_if, &server_ctx);

    while (-1 != (opt = getopt(argc, argv, PROG_OPTS "hr:Fn:e:p:z")))
    {
        switch (opt) {
        case 'z':
            compress_flag = 1;
            compression_type = optarg;
            break;
        case 'F':
            g_really_calculate_md5 = 0;
            break;
        case 'p':
            g_premature_close.stream_id = atoi(optarg);
            g_premature_close.limit = atoi(strchr(optarg, ':') + 1);
            break;
        case 'r':
            server_ctx.max_reqs = atoi(optarg);
            break;
        case 'e':
            server_ctx.expiry = time(NULL) + atoi(optarg);
            break;
        case 'n':
            server_ctx.n_conn = atoi(optarg);
            break;
        case 'h':
            usage(argv[0]);
            prog_print_common_options(&prog, stdout);
            exit(0);
        default:
            if (0 != prog_set_opt(&prog, opt, optarg))
                exit(1);
        }
    }

    add_alpn("md5");
    if (0 != prog_prep(&prog))
    {
        LSQ_ERROR("could not prep");
        exit(EXIT_FAILURE);
    }

    LSQ_DEBUG("entering event loop");

    s = prog_run(&prog);
    prog_cleanup(&prog);

    exit(0 == s ? EXIT_SUCCESS : EXIT_FAILURE);
}
