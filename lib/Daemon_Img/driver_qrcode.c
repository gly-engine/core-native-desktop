#include <stdlib.h>
#include <string.h>

#include <qrcodegen.h>

#include "gecnd.h"

static const char  qr_ecc_char[]   = "lmqh";
static const float qr_ecc_factor[] = { 0.38f, 0.32f, 0.24f, 0.18f };
static const enum qrcodegen_Ecc qr_ecc_level[] = {
    qrcodegen_Ecc_LOW, qrcodegen_Ecc_MEDIUM, qrcodegen_Ecc_QUARTILE, qrcodegen_Ecc_HIGH
};

typedef struct {
    int16_t     w;
    int16_t     h;
    uint8_t     ecc;
    const char *data;
    size_t      datalen;
} qr_request_t;

static inline bool qr_check_ecc(const char *s, size_t len, int *ecc) {
    if (len != 1) return false;
    for (int i = 0; qr_ecc_char[i]; i++)
        if (qr_ecc_char[i] == s[0]) { *ecc = i; return true; }
    return false;
}

static inline bool qr_check_size(int16_t w, int16_t h, int ecc, size_t datalen) {
    int usable = (w < h ? w * w : h * h) - 256;
    if (usable < 0) usable = 0;
    return datalen <= (size_t)(usable * qr_ecc_factor[ecc] / 8.0f);
}

static void driver_qr_parser(const char *url, void *pattern,
                       gamely_img_on_fetch_cb on_done, void *on_done_usr) {
                        
    struct { const char *ptr; size_t len; } str[3];
    gecnd_lang_rdsl_t ctx = {0};
    int16_t dim[2] = {0, 0};
    int ndim   = 0;
    int nstr   = 0;

    while (gecnd_lang_rdsl_iterator(&ctx, pattern, url)) {
        if (ctx.kind == GECND_TYPE_I16 && ndim < 2) {
            dim[ndim++] = ctx.val.i16;
        } else if (ctx.kind == GECND_TYPE_STRING && nstr < 3) {
            str[nstr].ptr = (const char *)ctx.val.ptr;
            str[nstr].len = ctx.val.len;
            nstr++;
        }
    }

    if (ctx.error || ndim != 2 || nstr < 2 || dim[0] <= 0 || dim[1] <= 0) {
        on_done(NULL, 0, NULL, on_done_usr);
        return;
    }

    int ecc;
    if (!qr_check_ecc(str[0].ptr, str[0].len, &ecc)) {
        on_done(NULL, 0, NULL, on_done_usr);
        return;
    }

    const char *data    = str[1].ptr;
    size_t      datalen = (size_t)(str[nstr - 1].ptr + str[nstr - 1].len - str[1].ptr);
    if (!qr_check_size(dim[0], dim[1], ecc, datalen)) {
        on_done(NULL, 0, NULL, on_done_usr);
        return;
    }

    qr_request_t *req = malloc(sizeof(*req));
    if (!req) { on_done(NULL, 0, NULL, on_done_usr); return; }
    req->w       = dim[0];
    req->h       = dim[1];
    req->ecc     = (uint8_t)ecc;
    req->data    = data;
    req->datalen = datalen;
    on_done((uint8_t *)req, sizeof(*req), "qrcode", on_done_usr);
}

static gamely_img_decoded_t driver_qr_decoder(const uint8_t *data, size_t len) {
    const qr_request_t *req = (const qr_request_t *)data;
    gamely_img_decoded_t out = {0};

    if (!data || len < sizeof(qr_request_t)) return out;
    if (req->w <= 0 || req->h <= 0 || req->ecc > 3) return out;

    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];

    if (!qrcodegen_encodeText(req->data, temp, qr, qr_ecc_level[req->ecc],
                              qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return out;

    int       qrsize = qrcodegen_getSize(qr);
    uint16_t *px     = malloc((size_t)req->w * (size_t)req->h * sizeof(uint16_t));

    if (!px) return out;
    for (int y = 0; y < req->h; y++) {
        int my = y * qrsize / req->h;
        for (int x = 0; x < req->w; x++) {
            int mx = x * qrsize / req->w;
            px[y * req->w + x] = qrcodegen_getModule(qr, mx, my) ? 0x0001 : 0xFFFF;
        }
    }

    out.pixels       = (uint8_t *)px;
    out.len          = (size_t)req->w * (size_t)req->h * sizeof(uint16_t);
    out.w            = req->w;
    out.h            = req->h;
    out.color_format = GECND_PIX_FMT_RGBA5551;
    
    return out;
}

__attribute__((constructor))
static void init() {
    gecnd_registry("set", "image_resolver:qr+$l+$i16+$i16$0",     driver_qr_parser, NULL);
    gecnd_registry("set", "image_resolver:qr+$l+$i16+$i16+$l$0",  driver_qr_parser, NULL);
    gecnd_registry("set", "image_decoder_sync:qrcode:rgba5551",   driver_qr_decoder, NULL);
}
