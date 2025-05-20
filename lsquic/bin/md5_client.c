/* Copyright (c) 2017 - 2022 LiteSpeed Technologies Inc.  See LICENSE. */
/*
 * md5_client.c -- This client sends one or more files to MD5 QUIC server
 *                 for MD5 sum calculation.
 */

#include <zlib.h>
#include <brotli/encode.h>
#include <zstd.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>

#ifndef WIN32
#include <unistd.h>
#include <fcntl.h>
#else
#include "vc_compat.h"
#include "getopt.h"
#endif

#include <event2/event.h>
#include <openssl/md5.h>

#include "lsquic.h"
#include "test_common.h"
#include "prog.h"

#include "../src/liblsquic/lsquic_logger.h"
#include "../src/liblsquic/lsquic_int_types.h"
#include "../src/liblsquic/lsquic_varint.h"
#include "../src/liblsquic/lsquic_hq.h"
#include "../src/liblsquic/lsquic_sfcw.h"
#include "../src/liblsquic/lsquic_hash.h"
#include "../src/liblsquic/lsquic_stream.h"

char *get_datetime_with_milliseconds();
char *compression_type = NULL; // added
int compress_flag = 0;
#define DATETIME_SIZE 24
#define BUFFER_SIZE 100 * 1024 * 1024 // Buffer size set to 100MB
/* Set to non-zero value to test out what happens when reset is sent */
#define RESET_AFTER_N_WRITES 0
char *PATH_TO_COMPRESSED_FILES = "/home/lars/Skola/examensarbete/QUIC_compress_bachelor_thesis-main/payloads/compressed";
static int g_write_file = 1;

//#define LOCAL_BUF_SIZE 0x100
#define LOCAL_BUF_SIZE 0x100
#define CHUNK 16384



static struct {
    unsigned    stream_id;  /* If set, reset this stream ID */
    off_t       offset;     /* Reset it after writing this many bytes */
} g_reset_stream;

struct file {
    LIST_ENTRY(file)        next_file;
    const char             *filename;
    struct lsquic_reader    reader;
    int                     fd;
    unsigned                priority;
    enum {
        FILE_RESET  = (1 << 0),
    }                       file_flags;
    size_t                  md5_off;
    char                    md5str[MD5_DIGEST_LENGTH * 2];
    char                    *buffer;     // Buffer to hold file data
    size_t                  buffer_size; // Size of the buffer
};

struct lsquic_conn_ctx;

struct client_ctx {
    struct lsquic_conn_ctx  *conn_h;
    LIST_HEAD(, file)            files;
    unsigned                     n_files;
    struct file                 *cur_file;
    lsquic_engine_t             *engine;
    struct service_port         *sport;
    struct prog                 *prog;
};

struct lsquic_conn_ctx {
    lsquic_conn_t       *conn;
    struct client_ctx   *client_ctx;
};


// starts new connection and creates two streams
static lsquic_conn_ctx_t *
client_on_new_conn (void *stream_if_ctx, lsquic_conn_t *conn)
{
    struct client_ctx *client_ctx = stream_if_ctx;
    lsquic_conn_ctx_t *conn_h = malloc(sizeof(*conn_h));
    conn_h->conn = conn;
    conn_h->client_ctx = client_ctx;
    client_ctx->conn_h = conn_h;
    assert(client_ctx->n_files > 0);
    unsigned n = client_ctx->n_files;
    while (n--)
        lsquic_conn_make_stream(conn);
    print_conn_info(conn);
    return conn_h;
}


static void
client_on_goaway_received (lsquic_conn_t *conn)
{
    //LSQ_NOTICE("GOAWAY received");
}


static void
client_on_conn_closed (lsquic_conn_t *conn)
{
    lsquic_conn_ctx_t *conn_h = lsquic_conn_get_ctx(conn);
    //LSQ_NOTICE("Connection closed");


    prog_stop(conn_h->client_ctx->prog);

    lsquic_conn_set_ctx(conn, NULL);
    free(conn_h);
}


struct lsquic_stream_ctx {
    lsquic_stream_t     *stream;
    struct client_ctx   *client_ctx;
    struct file         *file;
    struct event        *read_stdin_ev;
    struct {
        int         initialized;
        size_t      size,
                    off;
    }                    small;
};



// runs for every stream created
static lsquic_stream_ctx_t *
client_on_new_stream (void *stream_if_ctx, lsquic_stream_t *stream)
{
    struct client_ctx *const client_ctx = stream_if_ctx;
    if (!stream)
    {
        assert(client_ctx->n_files > 0);
        //LSQ_NOTICE("%s: got null stream: no more streams possible; # files: %u", __func__, client_ctx->n_files);
        //--client_ctx->n_files;
        if (0 == client_ctx->n_files)
        {
            LSQ_DEBUG("closing connection");
            lsquic_conn_close(client_ctx->conn_h->conn);
        }
        return NULL;
    }

    // stores connection etc
    lsquic_stream_ctx_t *st_h = calloc(1, sizeof(*st_h));
    st_h->stream = stream;
    st_h->client_ctx = stream_if_ctx;
    if (LIST_EMPTY(&st_h->client_ctx->files))
    {
        /* XXX: perhaps we should not be able to write immediately: there may
         * be internal memory constraints...
         */
        lsquic_stream_write(stream, "client request", 14);
        (void) lsquic_stream_flush(stream);
        lsquic_stream_wantwrite(stream, 0);
        lsquic_stream_wantread(stream, 1);
    }
    else
    {
        st_h->file = LIST_FIRST(&st_h->client_ctx->files);
        if (g_write_file)
        {

            st_h->file->fd = -1;
            st_h->file->reader.lsqr_read = test_reader_read;
            st_h->file->reader.lsqr_size = test_reader_size;
            st_h->file->reader.lsqr_ctx = create_lsquic_reader_ctx(st_h->file->filename);


            if (!st_h->file->reader.lsqr_ctx)
                exit(1);
        }
        else
        {
            st_h->file->fd = open(st_h->file->filename, O_RDONLY);
            if (st_h->file->fd < 0)
            {
                LSQ_ERROR("could not open %s for reading: %s",
                          st_h->file->filename, strerror(errno));
                exit(1);
            }
        }
        LIST_REMOVE(st_h->file, next_file);
        lsquic_stream_set_priority(stream, st_h->file->priority);
        lsquic_stream_wantwrite(stream, 1);
    }
    return st_h;
}


static size_t
buf_reader_size (void *reader_ctx)
{
    lsquic_stream_ctx_t *const st_h = reader_ctx;
    struct stat st;
    off_t off;

    if (st_h->small.initialized)
        goto initialized;

    if (0 != fstat(st_h->file->fd, &st))
    {
        LSQ_ERROR("fstat failed: %s", strerror(errno));
        goto err;
    }

    off = lseek(st_h->file->fd, 0, SEEK_CUR);
    if (off == (off_t) -1)
    {
        LSQ_ERROR("lseek failed: %s", strerror(errno));
        goto err;
    }

    if (st.st_size < off)
    {
        LSQ_ERROR("size mismatch");
        goto err;
    }

    st_h->small.initialized = 1;
    st_h->small.off = off;
    st_h->small.size = st.st_size;

  initialized:
    if (st_h->small.size - st_h->small.off > BUFFER_SIZE)
        return BUFFER_SIZE;
    else
        return st_h->small.size - st_h->small.off;

  err:
    close(st_h->file->fd);
    st_h->file->fd = 0;
    return 0;
}


static size_t
buf_reader_read (void *reader_ctx, void *buf, size_t count)
{
//printf("xd\n");
    lsquic_stream_ctx_t *const st_h = reader_ctx;
    ssize_t nr;
    unsigned char local_buf[LOCAL_BUF_SIZE];

    assert(st_h->small.initialized);

    if (count > sizeof(local_buf))
        count = sizeof(local_buf);

    nr = read(st_h->file->fd, local_buf, count);
    if (nr < 0)
    {
        LSQ_ERROR("read: %s", strerror(errno));
        close(st_h->file->fd);
        st_h->file->fd = 0;
        return 0;
    }


    memcpy(buf, local_buf, nr);
    st_h->small.off += nr;
    //printf("number of bytes read %ld\n", nr);
    return nr;
}


static void
client_file_on_write_buf (lsquic_stream_ctx_t *st_h)
{
    ssize_t nw;
    struct lsquic_reader reader = {
        .lsqr_read = buf_reader_read,
        .lsqr_size = buf_reader_size,
        .lsqr_ctx  = st_h,
    };

    if (g_reset_stream.stream_id == lsquic_stream_id(st_h->stream) &&
        lseek(st_h->file->fd, 0, SEEK_CUR) >= g_reset_stream.offset)
    {
        /* Note: this is an internal function */
        lsquic_stream_maybe_reset(st_h->stream,
                0x01 /* QUIC_INTERNAL_ERROR */, 1);
        g_reset_stream.stream_id = 0;   /* Reset only once */
    }

    nw = lsquic_stream_writef(st_h->stream, &reader);
    if (-1 == nw)
    {
        if (ECONNRESET == errno)
            st_h->file->file_flags |= FILE_RESET;
        LSQ_WARN("lsquic_stream_read: %s", strerror(errno));
        lsquic_stream_close(st_h->stream);
        return;
    }

#if RESET_AFTER_N_WRITES
    static int write_count = 0;
    if (write_count++ > RESET_AFTER_N_WRITES)
        lsquic_stream_reset(st_h->stream, 0);
#endif

    if (0 == nw)
    {
        (void) close(st_h->file->fd);
        if (0 == lsquic_stream_shutdown(st_h->stream, 1))
            lsquic_stream_wantread(st_h->stream, 1);
        else
        {
            if (ECONNRESET == errno)
                st_h->file->file_flags |= FILE_RESET;
            LSQ_WARN("lsquic_stream_shutdown: %s", strerror(errno));
            lsquic_stream_close(st_h->stream);
        }
    }
}


static void
client_file_on_write_efficient (lsquic_stream_t *stream,
                                                lsquic_stream_ctx_t *st_h)
{
    ssize_t nw;
    
    nw = lsquic_stream_writef(stream, &st_h->file->reader);

    //printf("printing nw %ld", nw);
    if (nw < 0)
    {
        LSQ_ERROR("write error: %s", strerror(errno));
        exit(1);
    }
    if (nw == 0)
    {
        destroy_lsquic_reader_ctx(st_h->file->reader.lsqr_ctx);
        st_h->file->reader.lsqr_ctx = NULL;
        if (0 == lsquic_stream_shutdown(st_h->stream, 1))
            lsquic_stream_wantread(st_h->stream, 1);
        else
        {
            if (ECONNRESET == errno)
                st_h->file->file_flags |= FILE_RESET;
            LSQ_WARN("lsquic_stream_shutdown: %s", strerror(errno));
            lsquic_stream_close(st_h->stream);
        }
    }
}


static void
client_file_on_write (lsquic_stream_t *stream, lsquic_stream_ctx_t *st_h)
{
    if (g_write_file)
        client_file_on_write_efficient(stream, st_h);
    else
        client_file_on_write_buf(st_h);
}


static void
client_file_on_read (lsquic_stream_t *stream, lsquic_stream_ctx_t *st_h)
{
    char buf;
    /* We expect to read in 32-character MD5 string */
    size_t ntoread = sizeof(st_h->file->md5str) - st_h->file->md5_off;
    if (0 == ntoread)
    {
        lsquic_stream_wantread(stream, 0);
        /* XXX What about an error (due to RST_STREAM) here: how are we to
         *     handle it?
         */
        /* Expect a FIN */
        if (0 == lsquic_stream_read(stream, &buf, sizeof(buf)))
        {
            //LSQ_NOTICE("%.*s  %s", (int) sizeof(st_h->file->md5str), st_h->file->md5str, st_h->file->filename);
            fflush(stdout);
            LSQ_DEBUG("# of files: %d", st_h->client_ctx->n_files);
            lsquic_stream_shutdown(stream, 0);
        }
        else
            LSQ_ERROR("expected FIN from stream!");
    }
    else
    {
        ssize_t nr = lsquic_stream_read(stream,
            st_h->file->md5str + st_h->file->md5_off, ntoread);
        if (-1 == nr)
        {
            if (ECONNRESET == errno)
                st_h->file->file_flags |= FILE_RESET;
            LSQ_WARN("lsquic_stream_read: %s", strerror(errno));
            lsquic_stream_close(stream);
            return;
        }
        else
            st_h->file->md5_off += nr;
    }
}


static void
client_file_on_close (lsquic_stream_t *stream, lsquic_stream_ctx_t *st_h)
{
    --st_h->client_ctx->n_files;
    //LSQ_NOTICE("%s called for stream %"PRIu64", # files: %u", __func__,
                        //lsquic_stream_id(stream), st_h->client_ctx->n_files);
    if (0 == st_h->client_ctx->n_files)
        lsquic_conn_close(st_h->client_ctx->conn_h->conn);
    if (!(st_h->file->file_flags & FILE_RESET) && 0 == RESET_AFTER_N_WRITES)
        assert(st_h->file->md5_off == sizeof(st_h->file->md5str));
    if (st_h->file->reader.lsqr_ctx)
    {
        destroy_lsquic_reader_ctx(st_h->file->reader.lsqr_ctx);
        st_h->file->reader.lsqr_ctx = NULL;
    }
    if (st_h->file->fd >= 0)
        (void) close(st_h->file->fd);
    free(st_h->file);
    free(st_h);
}


const struct lsquic_stream_if client_file_stream_if = {
    .on_new_conn            = client_on_new_conn,
    .on_goaway_received     = client_on_goaway_received,
    .on_conn_closed         = client_on_conn_closed,
    .on_new_stream          = client_on_new_stream,
    .on_read                = client_file_on_read,
    .on_write               = client_file_on_write,
    .on_close               = client_file_on_close,
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
"   -f FILE     File to send to the server -- must be specified at least\n"
"                 once.\n"
"   -b          Use buffering API for sending files over rather than\n"
"                 the efficient version.\n"
"   -p PRIORITY Applicatble to previous file specified with -f\n"
"   -r STREAM_ID:OFFSET\n"
"               Reset stream STREAM_ID after sending more that OFFSET bytes.\n"
            , prog);
}


char *get_datetime_with_milliseconds() {
    // Get the current time
    char *datetime_string = malloc(DATETIME_SIZE);
    struct timeval tv;
    gettimeofday(&tv, NULL);

    // Convert time to struct tm for formatting
    time_t rawtime = tv.tv_sec;
    struct tm *tm_info = localtime(&rawtime);

    // Format the time as a string with milliseconds
    strftime(datetime_string, DATETIME_SIZE, "%Y-%m-%d %H:%M:%S", tm_info);

    // Append milliseconds to the datetime string
    sprintf(datetime_string + strlen(datetime_string), ".%03d", (int)tv.tv_usec / 1000);
    return datetime_string;
}



int compress_file(char *infilename, char *outdirectory, char *outfilename) {
    FILE *infile = fopen(infilename, "rb");
    if (!infile) {
        fprintf(stderr, "Could not open %s for reading\n", infilename);
        return -1;
    }

    char outpath[1024];
    snprintf(outpath, sizeof(outpath), "%s/%s", outdirectory, outfilename);
    FILE *outfile = fopen(outpath, "wb");
    if (!outfile) {
        fprintf(stderr, "Could not open %s for writing\n", outpath);
        return -1;
    }

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
        fprintf(stderr, "Could not initialize zlib\n");
        return -1;
    }

    unsigned char in[CHUNK];
    unsigned char out[CHUNK];
    int flush;

    do {
        strm.avail_in = fread(in, 1, CHUNK, infile);
        if (ferror(infile)) {
            deflateEnd(&strm);
            return -1;
        }
        flush = feof(infile) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            deflate(&strm, flush);
            unsigned have = CHUNK - strm.avail_out;
            if (fwrite(out, 1, have, outfile) != have || ferror(outfile)) {
                deflateEnd(&strm);
                return -1;
            }
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);

    } while (flush != Z_FINISH);

    deflateEnd(&strm);
    fclose(infile);
    fclose(outfile);

    return 0;
}

//my changes, added both brotli and zstandard compression algorithms, the difference
//in the functions are the compression section, same otherwise. they are divided to improve
//readability.
int brotli_compress_file(char *infilename, char *outdirectory, char *outfilename){
    FILE *infile = fopen(infilename, "rb");
    if (!infile) {
        fprintf(stderr, "Could not open %s for reading\n", infilename);
        return -1;
    }

    char outpath[1024];
    snprintf(outpath, sizeof(outpath), "%s/%s", outdirectory, outfilename);
    FILE *outfile = fopen(outpath, "wb");
    if (!outfile) {
        fprintf(stderr, "Could not open %s for writing\n", outpath);
        return -1;
    }
    //calculate size of input, and read into memory.
    fseek(infile, 0, SEEK_END);
    size_t infile_size = ftell(infile);
    fseek(infile, 0, SEEK_SET);
    uint8_t *buffer_in = malloc(infile_size);
    if (!buffer_in){
        free(buffer_in);
        fclose(infile);
        fclose(outfile);
        return -1;
    }

    fread(buffer_in, 1, infile_size, infile);
    //calculate maximum size of the output file
    size_t max_out = BrotliEncoderMaxCompressedSize(infile_size);
    uint8_t *buffer_out = malloc(max_out);
    if (!buffer_out){
        free(buffer_in);
        free(buffer_out);
        fclose(infile);
        fclose(outfile);
        return -1;
    }
    //perform compression, and check if succesful.
    size_t size_out = max_out;
    BROTLI_BOOL res = BrotliEncoderCompress(3, BROTLI_DEFAULT_WINDOW , BROTLI_MODE_GENERIC, infile_size,buffer_in, &size_out, buffer_out);

    if (!res){
        fprintf(stderr, "brotli: compression failed");
        free(buffer_in);
        free(buffer_out);
        fclose(infile);
        fclose(outfile);
        return -1;

    }

    fwrite(buffer_out, 1, size_out, outfile);
    
    free(buffer_in);
    free(buffer_out);
    fclose(infile);
    fclose(outfile);

    return 0;
}

int zstandard_compress_file(char *infilename, char *outdirectory, char *outfilename){
    FILE *infile = fopen(infilename, "rb");
    if (!infile) {
        fprintf(stderr, "Could not open %s for reading\n", infilename);
        return -1;
    }

    char outpath[1024];
    snprintf(outpath, sizeof(outpath), "%s/%s", outdirectory, outfilename);
    FILE *outfile = fopen(outpath, "wb");
    if (!outfile) {
        fprintf(stderr, "Could not open %s for writing\n", outpath);
        return -1;
    }
    //calculate size of input, and read into memory.
    fseek(infile, 0, SEEK_END);
    size_t infile_size = ftell(infile);
    fseek(infile, 0, SEEK_SET);
    uint8_t *buffer_in = malloc(infile_size);
    if (!buffer_in){
        free(buffer_in);
        fclose(infile);
        fclose(outfile);
        return -1;
    }

    fread(buffer_in, 1, infile_size, infile);
    //calculate maximum size of the output file
    size_t max_out = ZSTD_compressBound(infile_size);
    uint8_t *buffer_out = malloc(max_out);
    if (!buffer_out){
        free(buffer_in);
        free(buffer_out);
        fclose(infile);
        fclose(outfile);
        return -1;
    }
    //perform compression of entire file at once, and check if succesful.
    size_t size_out = ZSTD_compress(buffer_out, max_out, buffer_in, infile_size, 3);

    if (ZSTD_isError(size_out)){
        fprintf(stderr, "ZSTD compression error: %s\n", ZSTD_getErrorName(size_out));
        free(buffer_in);
        free(buffer_out);
        fclose(infile);
        fclose(outfile);
        return -1;

    }

    fwrite(buffer_out, 1, size_out, outfile);
    
    free(buffer_in);
    free(buffer_out);
    fclose(infile);
    fclose(outfile);

    return 0;
}


int
main (int argc, char **argv)
{

    int opt, s;
    struct sport_head sports;
    struct prog prog;
    struct client_ctx client_ctx;
    struct file *file;

    file = NULL;
    memset(&client_ctx, 0, sizeof(client_ctx));
    client_ctx.prog = &prog;

    TAILQ_INIT(&sports);
    prog_init(&prog, 0, &sports, &client_file_stream_if, &client_ctx);
    prog.prog_api.ea_alpn = "md5";

    char *datetime = get_datetime_with_milliseconds();
    
    while (-1 != (opt = getopt(argc, argv, PROG_OPTS "bhr:f:p:z:")))
    {
        switch (opt) {
            case 'z':
            compression_type = optarg;
            compress_flag = 1; // Set the flag if -z is passed
            break;
            case 'p':
            if (file)
                    file->priority = atoi(optarg);
                    else
                    {
                        fprintf(stderr, "No file to apply priority to\n");
                    exit(1);
                }
                break;
                case 'b':
                g_write_file = 0;
                break;
                case 'f':
                char *filename = optarg;
                /* MY changes */
                char *new_file_path = malloc(256);
                if (compression_type) {
                    char *name = basename(filename);
                    if (strcmp(compression_type, "zlib") == 0) {
                        compress_file(filename, PATH_TO_COMPRESSED_FILES, name);
                    } else if (strcmp(compression_type, "brotli") == 0) {
                        brotli_compress_file(filename, PATH_TO_COMPRESSED_FILES, name);
                    } else if (strcmp(compression_type, "zstd") == 0) {
                        zstandard_compress_file(filename, PATH_TO_COMPRESSED_FILES, name);
                    } else {
                        fprintf(stderr, "Unknown compression type: %s\n", compression_type);
                        exit(1);
                    }
                    sprintf(new_file_path, "%s/%s", PATH_TO_COMPRESSED_FILES, name);
                    filename = new_file_path;
                }

                file = calloc(1, sizeof(*file));
                LIST_INSERT_HEAD(&client_ctx.files, file, next_file);
                ++client_ctx.n_files;
                file->filename = filename;
                //printf("\nfilename: %s\n\n", file->filename);
                break;
            case 'r':
                g_reset_stream.stream_id = atoi(optarg);
                g_reset_stream.offset = atoi(strchr(optarg, ':') + 1);
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

    if (LIST_EMPTY(&client_ctx.files))
    {
        fprintf(stderr, "please specify one of more files using -f\n");
        exit(1);
    }




    if (0 != prog_prep(&prog))
    {
        LSQ_ERROR("could not prep");
        exit(EXIT_FAILURE);
    }
    client_ctx.sport = TAILQ_FIRST(&sports);

    if (0 != prog_connect(&prog, NULL, 0))
    {
        LSQ_ERROR("could not connect");
        exit(EXIT_FAILURE);
    }

    LSQ_DEBUG("entering event loop");

    s = prog_run(&prog);
    prog_cleanup(&prog);

    // Output the string to stdout
    printf("|%s", datetime);
    free(datetime);
    exit(0 == s ? EXIT_SUCCESS : EXIT_FAILURE);

}
