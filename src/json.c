/* 极简 JSON 解析器：只支持 Open-Meteo 返回所需子集（对象/数组/字符串/数字/布尔/null）。
 * 字符串直接解码为 UTF-16（含 \uXXXX 与代理对），避免二次转换。 */

#include "json.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *s;
    size_t      n, i;
    int         depth;
    int         err;
} P;

/* ---- 宽字符动态缓冲 ---- */
typedef struct { wchar_t *p; size_t n, cap; } WB;

static int wb_reserve(WB *b, size_t need)
{
    if (b->n + need <= b->cap) return 1;
    size_t c = b->cap ? b->cap * 2 : 64;
    while (c < b->n + need) c *= 2;
    wchar_t *q = (wchar_t *)realloc(b->p, c * sizeof(wchar_t));
    if (!q) return 0;
    b->p = q;
    b->cap = c;
    return 1;
}

static int wb_ch(WB *b, wchar_t c)
{
    if (!wb_reserve(b, 1)) return 0;
    b->p[b->n++] = c;
    return 1;
}

static int wb_cp(WB *b, unsigned cp)
{
    if (cp > 0xFFFF) {
        cp -= 0x10000;
        return wb_ch(b, (wchar_t)(0xD800 + (cp >> 10))) &&
               wb_ch(b, (wchar_t)(0xDC00 + (cp & 0x3FF)));
    }
    return wb_ch(b, (wchar_t)cp);
}

static JNode *jnew(JType t)
{
    JNode *v = (JNode *)calloc(1, sizeof(JNode));
    if (v) v->type = t;
    return v;
}

static void skip_ws(P *p)
{
    while (p->i < p->n) {
        char c = p->s[p->i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->i++;
        else break;
    }
}

static unsigned utf8_next(P *p)
{
    const unsigned char *s = (const unsigned char *)p->s;
    unsigned c = s[p->i++];
    if (c < 0x80) return c;

    int extra;
    unsigned cp;
    if ((c & 0xE0) == 0xC0)      { cp = c & 0x1Fu; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
    else return 0xFFFD;

    while (extra-- > 0) {
        if (p->i >= p->n) return 0xFFFD;
        unsigned cc = s[p->i];
        if ((cc & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (cc & 0x3Fu);
        p->i++;
    }
    return cp;
}

static unsigned hex4(P *p)
{
    unsigned v = 0;
    for (int k = 0; k < 4; k++) {
        if (p->i >= p->n) { p->err = 1; return 0; }
        unsigned c = (unsigned char)p->s[p->i++];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else { p->err = 1; return 0; }
    }
    return v;
}

static wchar_t *parse_string(P *p)
{
    WB b = {0};

    if (p->i >= p->n || p->s[p->i] != '"') { p->err = 1; return NULL; }
    p->i++;

    while (p->i < p->n) {
        unsigned char c = (unsigned char)p->s[p->i];

        if (c == '"') {
            p->i++;
            if (!wb_reserve(&b, 1)) { free(b.p); p->err = 1; return NULL; }
            b.p[b.n] = 0;
            return b.p;
        }

        if (c == '\\') {
            p->i++;
            if (p->i >= p->n) break;
            char e = p->s[p->i++];

            if (e == 'u') {
                unsigned cp = hex4(p);
                if (p->err) break;
                if (cp >= 0xD800 && cp <= 0xDBFF && p->i + 1 < p->n &&
                    p->s[p->i] == '\\' && p->s[p->i + 1] == 'u') {
                    size_t save = p->i;
                    p->i += 2;
                    unsigned lo = hex4(p);
                    if (!p->err && lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                    else
                        p->i = save;
                }
                if (!wb_cp(&b, cp)) p->err = 1;
                if (p->err) break;
                continue;
            }

            wchar_t ch;
            switch (e) {
            case '"':  ch = L'"';  break;
            case '\\': ch = L'\\'; break;
            case '/':  ch = L'/';  break;
            case 'b':  ch = L'\b'; break;
            case 'f':  ch = L'\f'; break;
            case 'n':  ch = L'\n'; break;
            case 'r':  ch = L'\r'; break;
            case 't':  ch = L'\t'; break;
            default:   p->err = 1; ch = 0; break;
            }
            if (p->err) break;
            if (!wb_ch(&b, ch)) { p->err = 1; break; }
            continue;
        }

        if (!wb_cp(&b, utf8_next(p))) { p->err = 1; break; }
    }

    free(b.p);
    p->err = 1;
    return NULL;
}

static JNode *parse_value(P *p);

static JNode *parse_array(P *p)
{
    JNode *a = jnew(JS_ARR);
    if (!a) { p->err = 1; return NULL; }
    p->i++;                    /* '[' */
    skip_ws(p);

    if (p->i < p->n && p->s[p->i] == ']') { p->i++; return a; }

    for (;;) {
        JNode *v = parse_value(p);
        if (!v) { json_free(a); return NULL; }
        JNode **q = (JNode **)realloc(a->items, (size_t)(a->count + 1) * sizeof(JNode *));
        if (!q) { json_free(v); json_free(a); p->err = 1; return NULL; }
        a->items = q;
        a->items[a->count++] = v;

        skip_ws(p);
        if (p->i < p->n && p->s[p->i] == ',') { p->i++; skip_ws(p); continue; }
        if (p->i < p->n && p->s[p->i] == ']') { p->i++; return a; }

        json_free(a);
        p->err = 1;
        return NULL;
    }
}

static JNode *parse_object(P *p)
{
    JNode *o = jnew(JS_OBJ);
    if (!o) { p->err = 1; return NULL; }
    p->i++;                    /* '{' */
    skip_ws(p);

    if (p->i < p->n && p->s[p->i] == '}') { p->i++; return o; }

    for (;;) {
        skip_ws(p);
        wchar_t *k = parse_string(p);
        if (!k) { json_free(o); return NULL; }

        skip_ws(p);
        if (p->i >= p->n || p->s[p->i] != ':') { free(k); json_free(o); p->err = 1; return NULL; }
        p->i++;

        JNode *v = parse_value(p);
        if (!v) { free(k); json_free(o); return NULL; }

        JNode **qi = (JNode **)realloc(o->items, (size_t)(o->count + 1) * sizeof(JNode *));
        wchar_t **qk = (wchar_t **)realloc(o->keys, (size_t)(o->count + 1) * sizeof(wchar_t *));
        if (!qi || !qk) {
            if (qi) o->items = qi;
            if (qk) o->keys = qk;
            free(k);
            json_free(v);
            json_free(o);
            p->err = 1;
            return NULL;
        }
        o->items = qi;
        o->keys = qk;
        o->keys[o->count] = k;
        o->items[o->count] = v;
        o->count++;

        skip_ws(p);
        if (p->i < p->n && p->s[p->i] == ',') { p->i++; continue; }
        if (p->i < p->n && p->s[p->i] == '}') { p->i++; return o; }

        json_free(o);
        p->err = 1;
        return NULL;
    }
}

static JNode *parse_value(P *p)
{
    skip_ws(p);
    if (p->i >= p->n)    { p->err = 1; return NULL; }
    if (p->depth > 64)   { p->err = 1; return NULL; }

    char c = p->s[p->i];

    if (c == '{') {
        p->depth++;
        JNode *v = parse_object(p);
        p->depth--;
        return v;
    }
    if (c == '[') {
        p->depth++;
        JNode *v = parse_array(p);
        p->depth--;
        return v;
    }
    if (c == '"') {
        JNode *v = jnew(JS_STR);
        if (!v) { p->err = 1; return NULL; }
        v->str = parse_string(p);
        if (!v->str) { free(v); return NULL; }
        return v;
    }
    if (c == 't' && p->i + 4 <= p->n && !memcmp(p->s + p->i, "true", 4)) {
        p->i += 4;
        JNode *v = jnew(JS_BOOL);
        if (v) v->num = 1; else p->err = 1;
        return v;
    }
    if (c == 'f' && p->i + 5 <= p->n && !memcmp(p->s + p->i, "false", 5)) {
        p->i += 5;
        JNode *v = jnew(JS_BOOL);
        if (v) v->num = 0; else p->err = 1;
        return v;
    }
    if (c == 'n' && p->i + 4 <= p->n && !memcmp(p->s + p->i, "null", 4)) {
        p->i += 4;
        return jnew(JS_NULL);
    }

    size_t st = p->i;
    if (c == '-' || c == '+') p->i++;
    while (p->i < p->n) {
        char d = p->s[p->i];
        if ((d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-') p->i++;
        else break;
    }
    if (p->i == st) { p->err = 1; return NULL; }

    size_t len = p->i - st;
    char tmp[64];
    if (len >= sizeof(tmp)) { p->err = 1; return NULL; }
    memcpy(tmp, p->s + st, len);
    tmp[len] = 0;

    JNode *v = jnew(JS_NUM);
    if (!v) { p->err = 1; return NULL; }
    v->num = strtod(tmp, NULL);
    return v;
}

JNode *json_parse(const char *s, size_t n)
{
    if (!s || n == 0) return NULL;
    P p;
    memset(&p, 0, sizeof(p));
    p.s = s;
    p.n = n;

    JNode *root = parse_value(&p);
    if (!root || p.err) { json_free(root); return NULL; }
    return root;
}

void json_free(JNode *v)
{
    if (!v) return;
    for (int i = 0; i < v->count; i++) {
        json_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->str);
    free(v);
}

JNode *json_obj(const JNode *o, const wchar_t *key)
{
    if (!o || o->type != JS_OBJ || !key) return NULL;
    for (int i = 0; i < o->count; i++) {
        if (o->keys[i] && !wcscmp(o->keys[i], key)) return o->items[i];
    }
    return NULL;
}

JNode *json_at(const JNode *a, int i)
{
    if (!a || a->type != JS_ARR || i < 0 || i >= a->count) return NULL;
    return a->items[i];
}

int json_count(const JNode *v)
{
    if (!v || (v->type != JS_ARR && v->type != JS_OBJ)) return 0;
    return v->count;
}

double json_num(const JNode *v, double def)
{
    if (!v) return def;
    if (v->type == JS_NUM || v->type == JS_BOOL) return v->num;
    if (v->type == JS_STR && v->str) return wcstod(v->str, NULL);
    return def;
}

const wchar_t *json_str(const JNode *v, const wchar_t *def)
{
    if (!v || v->type != JS_STR || !v->str) return def;
    return v->str;
}
