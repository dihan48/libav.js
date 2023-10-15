/*
 * JavaScript fetch metaprotocol for ffmpeg client
 * Copyright (c) 2023 Yahweasel and contributors
 *
 * This file is part of FFmpeg in libav.js. The following license applies only
 * to this file.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"
#include "config_components.h"

#include "libavutil/error.h"
#include "libavutil/opt.h"

#include "url.h"

#include <emscripten.h>
#include <errno.h>

typedef struct JSFetchContext {
    const AVClass *class;
    // All of the real information is stored in a JavaScript structure
    int idx;
} JSFetchContext;

static const AVOption options[] = {
    { NULL }
};

#if CONFIG_JSFETCH_PROTOCOL
static const AVClass jsfetch_context_class = {
    .class_name = "jsfetch",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT
};

/**
 * Open a fetch connection (JavaScript side).
 */
EM_JS(int, jsfetch_open_js, (const char *url), {
    return Asyncify.handleAsync(function() {
        return Promise.all([]).then(function() {
            url = UTF8ToString(url);
            if (url.slice(0, 8) === "jsfetch:")
                return fetch(url.slice(8));
            else
                return fetch(url);
        }).then(function(response) {
            if (!Module.libavjsJSFetch)
                Module.libavjsJSFetch = {ctr: 1, fetches: {}};
            var jsf = Module.libavjsJSFetch;
            var idx = jsf.ctr++;
            var reader = response.body.getReader();
            var jsfo = jsf.fetches[idx] = {
                url: url,
                response: response,
                reader: reader,
                next: reader.read().then(function(res) {
                    jsfo.buf = res;
                }).catch(function(rej) {
                    jsfo.rej = rej;
                }),
                buf: null,
                rej: null
            };
            return idx;
        }).catch(function(ex) {
            Module.fsThrownError = ex;
            console.error(ex);
            return -11 /* ECANCELED */;
        });
    });
});

/**
 * Open a fetch connection.
 */
static int jsfetch_open(URLContext *h, const char *url, int flags, AVDictionary **options)
{
    JSFetchContext *ctx = h->priv_data;
    h->is_streamed = 1;
    ctx->idx = jsfetch_open_js(url);
    return (ctx->idx > 0) ? 0 : ctx->idx;
}

/**
 * Read from a fetch connection (JavaScript side).
 */
EM_JS(int, jsfetch_read_js, (int idx, unsigned char *toBuf, int size), {
    return Asyncify.handleAsync(function() { return Promise.all([]).then(function() {
        var jsfo = Module.libavjsJSFetch.fetches[idx];
        if (jsfo.buf || jsfo.rej) {
            // Already have data
            var fromBuf = jsfo.buf;
            var rej = jsfo.rej;

            if (fromBuf) {
                if (fromBuf.done) {
                    // EOF
                    return 0;
                }
                if (fromBuf.value.length > size) {
                    // Return some of the buffer
                    Module.HEAPU8.set(fromBuf.value.subarray(0, size), toBuf);
                    fromBuf.value = fromBuf.value.subarray(size);
                    return size;
                }

                /* Otherwise, return the remainder of the buffer and start
                 * the next read */
                var ret = fromBuf.value.length;
                Module.HEAPU8.set(fromBuf.value, toBuf);
                jsfo.buf = jsfo.rej = null;
                jsfo.next = jsfo.reader.read().then(function(res) {
                    jsfo.buf = res;
                }).catch(function(rej) {
                    jsfo.rej = rej;
                });
                return ret;
            }

            // Otherwise, there was an error
            Module.fsThrownError = rej;
            console.error(rej);
            return -11 /* ECANCELED */;
        }

        // The next data isn't available yet. Force them to wait.
        return new Promise(function(res) {
            setTimeout(function() { res(-6 /* EAGAIN */); }, 100);
        });
    }); });
});

/**
 * Read from a fetch connection.
 */
static int jsfetch_read(URLContext *h, unsigned char *buf, int size)
{
    JSFetchContext *ctx = h->priv_data;
    return jsfetch_read_js(ctx->idx, buf, size);
}

/**
 * Close a fetch connection (JavaScript side).
 */
EM_JS(void, jsfetch_close_js, (int idx), {
    var jsfo = Module.libavjsJSFetch.fetches[idx];
    if (jsfo) {
        try { jsfo.reader.cancel(); } catch (ex) {}
        delete Module.libavjsJSFetch.fetches[idx];
    }
});

/**
 * Close a fetch connection.
 */
static int jsfetch_close(URLContext *h)
{
    JSFetchContext *ctx = h->priv_data;
    jsfetch_close_js(ctx->idx);
    return 0;
}

const URLProtocol ff_jsfetch_protocol = {
    .name               = "jsfetch",
    .url_open2          = jsfetch_open,
    .url_read           = jsfetch_read,
    .url_close          = jsfetch_close,
    .priv_data_size     = sizeof(JSFetchContext),
    .priv_data_class    = &jsfetch_context_class,
    .flags              = URL_PROTOCOL_FLAG_NETWORK,
    .default_whitelist  = "http,https"
};
#endif
