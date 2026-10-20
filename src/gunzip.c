/* 极简 gzip / deflate 解压（RFC 1951 / 1952）
 *
 * 为什么自己写：和风天气无视 Accept-Encoding: identity，始终返回 gzip；
 * 而 WinHTTP 的 WINHTTP_OPTION_DECOMPRESSION 在"未压缩响应"上会直接 abort，
 * 导致 Open-Meteo 那条链路报 E_ABORT。自己解码可以两条链路并存，且不引第三方库。 */

#include "app.h"

typedef struct {
    const unsigned char *in;
    size_t n, pos;
    unsigned bitbuf;
    int bitcnt;
    unsigned char *out;
    size_t olen, ocap;
    int err;
} Inf;

static int inf_put(Inf *s, unsigned char c)
{
    if (s->olen >= s->ocap) {
        size_t nc = s->ocap ? s->ocap * 2 : 4096;
        unsigned char *p = (unsigned char *)realloc(s->out, nc);
        if (!p) { s->err = 1; return 0; }
        s->out  = p;
        s->ocap = nc;
    }
    s->out[s->olen++] = c;
    return 1;
}

static int inf_bits(Inf *s, int need)
{
    while (s->bitcnt < need) {
        if (s->pos >= s->n) { s->err = 1; return 0; }
        s->bitbuf |= (unsigned)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    unsigned v = s->bitbuf & ((1u << need) - 1u);
    s->bitbuf >>= need;
    s->bitcnt -= need;
    return (int)v;
}

/* 规范 Huffman 表（puff.c 的做法） */
typedef struct {
    short count[16];
    short symbol[288];
} Huf;

static void huf_build(Huf *h, const unsigned char *lens, int n)
{
    int offs[16];

    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++) h->count[lens[i]]++;
    h->count[0] = 0;

    offs[0] = offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h->count[len];
    for (int i = 0; i < n; i++)
        if (lens[i]) h->symbol[offs[lens[i]]++] = (short)i;
}

static int huf_decode(Inf *s, const Huf *h)
{
    int code = 0, first = 0, index = 0;

    for (int len = 1; len < 16; len++) {
        code |= inf_bits(s, 1);
        if (s->err) return -1;

        int count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->err = 1;
    return -1;
}

static const short LBASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const short LEXT[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const short DBASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const short DEXT[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static int inf_codes(Inf *s, const Huf *lencode, const Huf *distcode)
{
    for (;;) {
        int sym = huf_decode(s, lencode);
        if (sym < 0) return 0;

        if (sym < 256) {
            if (!inf_put(s, (unsigned char)sym)) return 0;
        } else if (sym == 256) {
            return 1;
        } else {
            sym -= 257;
            if (sym >= 29) { s->err = 1; return 0; }

            int len = LBASE[sym] + inf_bits(s, LEXT[sym]);
            int dsym = huf_decode(s, distcode);
            if (dsym < 0 || dsym >= 30) { s->err = 1; return 0; }

            int dist = DBASE[dsym] + inf_bits(s, DEXT[dsym]);
            if (s->err) return 0;
            if (dist <= 0 || (size_t)dist > s->olen) { s->err = 1; return 0; }

            for (int i = 0; i < len; i++)
                if (!inf_put(s, s->out[s->olen - (size_t)dist])) return 0;
        }
    }
}

static int inf_stored(Inf *s)
{
    /* 丢弃到字节边界 */
    s->bitbuf = 0;
    s->bitcnt = 0;

    if (s->pos + 4 > s->n) { s->err = 1; return 0; }

    unsigned len  = (unsigned)s->in[s->pos] | ((unsigned)s->in[s->pos + 1] << 8);
    unsigned nlen = (unsigned)s->in[s->pos + 2] | ((unsigned)s->in[s->pos + 3] << 8);
    s->pos += 4;
    if ((len ^ 0xFFFFu) != nlen) { s->err = 1; return 0; }
    if (s->pos + len > s->n) { s->err = 1; return 0; }

    for (unsigned i = 0; i < len; i++)
        if (!inf_put(s, s->in[s->pos++])) return 0;
    return 1;
}

static void huf_fixed(Huf *lencode, Huf *distcode)
{
    unsigned char lens[288];
    int i = 0;

    for (; i < 144; i++) lens[i] = 8;
    for (; i < 256; i++) lens[i] = 9;
    for (; i < 280; i++) lens[i] = 7;
    for (; i < 288; i++) lens[i] = 8;
    huf_build(lencode, lens, 288);

    for (i = 0; i < 30; i++) lens[i] = 5;
    huf_build(distcode, lens, 30);
}

static int inf_dynamic(Inf *s)
{
    static const short ORDER[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    unsigned char lens[320];
    Huf lencode, distcode;

    int nlen  = inf_bits(s, 5) + 257;
    int ndist = inf_bits(s, 5) + 1;
    int ncode = inf_bits(s, 4) + 4;
    if (s->err || nlen > 286 || ndist > 30) { s->err = 1; return 0; }

    memset(lens, 0, sizeof(lens));
    for (int i = 0; i < ncode; i++) lens[ORDER[i]] = (unsigned char)inf_bits(s, 3);
    if (s->err) return 0;

    huf_build(&lencode, lens, 19);

    int idx = 0;
    while (idx < nlen + ndist) {
        int sym = huf_decode(s, &lencode);
        if (sym < 0) return 0;

        if (sym < 16) {
            lens[idx++] = (unsigned char)sym;
            continue;
        }

        int len = 0, rep;
        if (sym == 16) {
            if (idx == 0) { s->err = 1; return 0; }
            len = lens[idx - 1];
            rep = 3 + inf_bits(s, 2);
        } else if (sym == 17) {
            rep = 3 + inf_bits(s, 3);
        } else {
            rep = 11 + inf_bits(s, 7);
        }
        if (s->err || idx + rep > nlen + ndist) { s->err = 1; return 0; }
        while (rep--) lens[idx++] = (unsigned char)len;
    }

    if (lens[256] == 0) { s->err = 1; return 0; }   /* 缺结束码 */

    huf_build(&lencode, lens, nlen);
    huf_build(&distcode, lens + nlen, ndist);
    return inf_codes(s, &lencode, &distcode);
}

static unsigned char *inf_run(const unsigned char *in, size_t n, size_t start, size_t *outlen)
{
    Inf s;
    memset(&s, 0, sizeof(s));
    s.in  = in;
    s.n   = n;
    s.pos = start;

    int last = 0;
    while (!last && !s.err) {
        last = inf_bits(&s, 1);
        if (s.err) break;

        int type = inf_bits(&s, 2);
        if (s.err) break;

        if (type == 0) {
            if (!inf_stored(&s)) break;
        } else if (type == 1) {
            Huf l, d;
            huf_fixed(&l, &d);
            if (!inf_codes(&s, &l, &d)) break;
        } else if (type == 2) {
            if (!inf_dynamic(&s)) break;
        } else {
            s.err = 1;
            break;
        }
    }

    if (s.err || !s.out) { free(s.out); return NULL; }
    *outlen = s.olen;
    return s.out;
}

unsigned char *gz_decompress(const unsigned char *in, size_t n, size_t *outlen)
{
    if (!in || n < 6 || !outlen) return NULL;

    size_t start;

    if (in[0] == 0x1f && in[1] == 0x8b && in[2] == 8) {          /* gzip */
        int flg = in[3];
        size_t p = 10;

        if (flg & 4) {
            if (p + 2 > n) return NULL;
            size_t xlen = (size_t)in[p] | ((size_t)in[p + 1] << 8);
            p += 2 + xlen;
        }
        if (flg & 8)  { while (p < n && in[p]) p++; p++; }        /* FNAME */
        if (flg & 16) { while (p < n && in[p]) p++; p++; }        /* FCOMMENT */
        if (flg & 2)  p += 2;                                     /* FHCRC */
        if (p >= n) return NULL;
        start = p;
    } else if (in[0] == 0x78 && ((((unsigned)in[0] << 8) | in[1]) % 31) == 0) {
        start = 2;                                               /* zlib */
    } else {
        return NULL;                                             /* 未压缩 */
    }

    return inf_run(in, n, start, outlen);
}
