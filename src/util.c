/* 字符串工具 */

#include "app.h"

#include <stdio.h>

wchar_t *u8_to_w(const char *s, int n)
{
    if (!s) return NULL;
    int len = (n < 0) ? (int)strlen(s) : n;
    if (len < 0) len = 0;

    int need = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    wchar_t *w = (wchar_t *)calloc((size_t)(need > 0 ? need : 0) + 1, sizeof(wchar_t));
    if (!w) return NULL;
    if (need > 0) MultiByteToWideChar(CP_UTF8, 0, s, len, w, need);
    w[need > 0 ? need : 0] = 0;
    return w;
}

char *w_to_u8(const wchar_t *s)
{
    if (!s) return NULL;
    int need = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (need <= 0) return NULL;
    char *a = (char *)calloc((size_t)need, 1);
    if (!a) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, s, -1, a, need, NULL, NULL);
    return a;
}

void trim_w(wchar_t *s)
{
    if (!s) return;
    wchar_t *p = s;
    while (*p == L' ' || *p == L'\t' || *p == 0x3000) p++;
    if (p != s) memmove(s, p, (wcslen(p) + 1) * sizeof(wchar_t));

    size_t n = wcslen(s);
    while (n > 0) {
        wchar_t c = s[n - 1];
        if (c == L' ' || c == L'\t' || c == 0x3000) s[--n] = 0;
        else break;
    }
}

/* "杭州 · 浙江" -> "杭州"（标签栏用短名） */
void city_short(const wchar_t *full, wchar_t *out, int cap)
{
    if (!out || cap <= 0) return;
    if (!full) { out[0] = 0; return; }

    const wchar_t *sep = wcsstr(full, L" · ");
    size_t n = sep ? (size_t)(sep - full) : wcslen(full);
    if (n > (size_t)cap - 1) n = (size_t)cap - 1;
    memcpy(out, full, n * sizeof(wchar_t));
    out[n] = 0;
}
