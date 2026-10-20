/* WinHTTP 封装：同步 GET，返回 UTF-8 文本（调用方负责 http_free） */

#include "app.h"

#include <winhttp.h>
#include <stdio.h>
#include <stdarg.h>

#pragma comment(lib, "winhttp.lib")

#define MAX_BODY (8u * 1024u * 1024u)

static void seterr(wchar_t *err, int cap, const wchar_t *fmt, ...)
{
    if (!err || cap <= 0) return;
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(err, (size_t)cap - 1, fmt, ap);
    err[cap - 1] = 0;
    va_end(ap);
}

char *http_get(const wchar_t *url, size_t *out_len, wchar_t *err, int errcap)
{
    HINTERNET hs = NULL, hc = NULL, hr = NULL;
    char *body = NULL;
    size_t len = 0, cap = 0;
    wchar_t host[256] = L"";
    wchar_t path[1024] = L"/";
    INTERNET_PORT port;
    DWORD flags;
    DWORD status = 0, slen = sizeof(status);

    if (err && errcap > 0) err[0] = 0;

    /* 手工拆解 URL，避免 WinHttpCrackUrl 的缓冲区约定问题 */
    const wchar_t *p = url;
    BOOL secure;
    if (!wcsncmp(p, L"https://", 8))     { secure = TRUE;  p += 8; }
    else if (!wcsncmp(p, L"http://", 7)) { secure = FALSE; p += 7; }
    else { seterr(err, errcap, L"仅支持 http/https"); return NULL; }

    const wchar_t *slash = wcschr(p, L'/');
    size_t hlen = slash ? (size_t)(slash - p) : wcslen(p);
    if (hlen == 0 || hlen >= 256) { seterr(err, errcap, L"URL 主机无效"); return NULL; }
    memcpy(host, p, hlen * sizeof(wchar_t));
    host[hlen] = 0;
    if (slash) wcsncpy(path, slash, 1023);

    port = secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
    wchar_t *colon = wcschr(host, L':');
    if (colon) {
        *colon = 0;
        port = (INTERNET_PORT)wcstol(colon + 1, NULL, 10);
    }
    flags = secure ? WINHTTP_FLAG_SECURE : 0;

    hs = WinHttpOpen(L"MiniWeather/1.0 (Win32)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) {
        hs = WinHttpOpen(L"MiniWeather/1.0 (Win32)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!hs) { seterr(err, errcap, L"初始化网络失败（%lu）", GetLastError()); goto done; }

    WinHttpSetTimeouts(hs, 8000, 8000, 10000, 15000);

    hc = WinHttpConnect(hs, host, port, 0);
    if (!hc) { seterr(err, errcap, L"连接失败（%lu）", GetLastError()); goto done; }

    hr = WinHttpOpenRequest(hc, L"GET", path, NULL, WINHTTP_NO_REFERER,
                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hr) { seterr(err, errcap, L"创建请求失败（%lu）", GetLastError()); goto done; }

    WinHttpAddRequestHeaders(hr,
        L"Accept: application/json\r\nAccept-Encoding: identity\r\n",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(hr, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        seterr(err, errcap, L"发送请求失败（%lu）", GetLastError());
        goto done;
    }
    if (!WinHttpReceiveResponse(hr, NULL)) {
        seterr(err, errcap, L"接收响应失败（%lu）", GetLastError());
        goto done;
    }
    if (!WinHttpQueryHeaders(hr, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX)) {
        seterr(err, errcap, L"读取状态码失败（%lu）", GetLastError());
        goto done;
    }
    if (status != 200) {
        seterr(err, errcap, L"服务器返回 HTTP %lu", status);
        goto done;
    }

    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(hr, &avail)) {
            seterr(err, errcap, L"读取数据失败（%lu）", GetLastError());
            goto done;
        }
        if (avail == 0) break;
        if (len + avail + 1 > MAX_BODY) { seterr(err, errcap, L"响应过大"); goto done; }

        if (len + avail + 1 > cap) {
            size_t nc = cap ? cap : 8192;
            while (nc < len + avail + 1) nc *= 2;
            char *nb = (char *)realloc(body, nc);
            if (!nb) { seterr(err, errcap, L"内存不足"); goto done; }
            body = nb;
            cap = nc;
        }
        if (!WinHttpReadData(hr, body + len, avail, &got)) {
            seterr(err, errcap, L"读取数据失败（%lu）", GetLastError());
            goto done;
        }
        if (got == 0) break;
        len += got;
    }

    if (!body) {
        body = (char *)calloc(1, 1);
        if (!body) { seterr(err, errcap, L"内存不足"); goto done; }
        cap = 1;
    }
    body[len] = 0;
    if (out_len) *out_len = len;

done:
    if (hr) WinHttpCloseHandle(hr);
    if (hc) WinHttpCloseHandle(hc);
    if (hs) WinHttpCloseHandle(hs);

    if (body && len == 0 && err && err[0]) {
        free(body);
        body = NULL;
    }
    return body;
}

void http_free(char *p)
{
    free(p);
}
