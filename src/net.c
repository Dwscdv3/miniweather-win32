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

/* 取系统版本号。不能用 GetVersionEx：没有 manifest 时在 8.1+ 会谎报 6.2。
 * RtlGetVersion 是 ntdll 的导出，从 XP 到 11 都存在，行为稳定。 */
static int os_at_least(int major, int minor)
{
    typedef struct {
        ULONG  dwOSVersionInfoSize;
        ULONG  dwMajorVersion;
        ULONG  dwMinorVersion;
        ULONG  dwBuildNumber;
        ULONG  dwPlatformId;
        WCHAR  szCSDVersion[128];
    } OSVI;

    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    if (!nt) return 0;

    typedef LONG (WINAPI *PFN_RTLVER)(void *);
    PFN_RTLVER fn = (PFN_RTLVER)(void *)GetProcAddress(nt, "RtlGetVersion");
    if (!fn) return 0;

    OSVI vi;
    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn(&vi) != 0) return 0;

    if (vi.dwMajorVersion != (ULONG)major) return vi.dwMajorVersion > (ULONG)major;
    return vi.dwMinorVersion >= (ULONG)minor;
}

/* 显式启用 TLS 1.2。
 * Windows 7 的 WinHTTP 默认只协商 TLS 1.0，而 Open-Meteo 与和风天气都要求 TLS 1.2+，
 * 不设这一项在 Win7 上会握手失败（与 32/64 位无关）。 */
static void enable_tls12(HINTERNET hr)
{
    DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    DWORD both = protos | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (WinHttpSetOption(hr, WINHTTP_OPTION_SECURE_PROTOCOLS, &both, sizeof(both)))
        return;
#endif
    WinHttpSetOption(hr, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
}

char *http_get_ex(const wchar_t *url, const wchar_t *extra_headers,
                  size_t *out_len, wchar_t *err, int errcap)
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

    /* Win8.1 才有自动代理（WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY）；更早的系统直接用
     * 系统默认代理，避免先失败一次再回退 */
    DWORD access = os_at_least(6, 3) ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
                                     : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    hs = WinHttpOpen(L"MiniWeather/1.0 (Win32)", access,
                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs && access != WINHTTP_ACCESS_TYPE_DEFAULT_PROXY) {
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

    if (secure) enable_tls12(hr);

    /* 首选不压缩；和风会无视 identity 强制 gzip，由 WinHTTP 自动解压兜底
     * （注意：Open-Meteo 的 deflate 响应在 WinHTTP 解压路径上会报 E_ABORT） */
    WinHttpAddRequestHeaders(hr,
        L"Accept: application/json\r\nAccept-Encoding: identity\r\n",
        (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (extra_headers && extra_headers[0])
        WinHttpAddRequestHeaders(hr, extra_headers, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

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

    /* 服务端强制压缩时本地解压（和风天气会忽略 identity） */
    if (len > 4 && ((unsigned char)body[0] == 0x1f || (unsigned char)body[0] == 0x78)) {
        size_t dn = 0;
        unsigned char *d = gz_decompress((const unsigned char *)body, len, &dn);
        if (d) {
            char *nb = (char *)malloc(dn + 1);
            if (nb) {
                memcpy(nb, d, dn);
                nb[dn] = 0;
                free(body);
                body = nb;
                len  = dn;
                cap  = dn + 1;
            }
            free(d);
        }
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

char *http_get(const wchar_t *url, size_t *out_len, wchar_t *err, int errcap)
{
    return http_get_ex(url, NULL, out_len, err, errcap);
}
