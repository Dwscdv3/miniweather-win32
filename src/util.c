/* 字符串 / 配置文件等通用工具 */

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

/* 配置目录：%APPDATA%\MiniWeather\config.txt */
static void config_dir(wchar_t *out, int cap)
{
    wchar_t dir[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        GetModuleFileNameW(NULL, dir, MAX_PATH);
        wchar_t *p = wcsrchr(dir, L'\\');
        if (p) *p = 0;
    }
    swprintf(out, (size_t)cap, L"%s\\MiniWeather", dir);
    CreateDirectoryW(out, NULL);
}

void config_load(wchar_t *city, int cap, double *lat, double *lon)
{
    wcscpy(city, L"北京");
    *lat = 39.9042;
    *lon = 116.4074;

    wchar_t dir[MAX_PATH + 32], path[MAX_PATH + 64];
    config_dir(dir, MAX_PATH + 32);
    swprintf(path, MAX_PATH + 64, L"%s\\config.txt", dir);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;

    char buf[4096];
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);
    buf[got < sizeof(buf) ? got : sizeof(buf) - 1] = 0;

    wchar_t *w = u8_to_w(buf, (int)got);
    if (!w) return;

    wchar_t *ctx = NULL;
    for (wchar_t *line = wcstok(w, L"\r\n", &ctx); line; line = wcstok(NULL, L"\r\n", &ctx)) {
        wchar_t *eq = wcschr(line, L'=');
        if (!eq) continue;
        *eq = 0;
        trim_w(line);
        wchar_t *val = eq + 1;
        trim_w(val);

        if (!wcscmp(line, L"city") && val[0]) {
            wcsncpy(city, val, (size_t)cap - 1);
            city[cap - 1] = 0;
        } else if (!wcscmp(line, L"lat")) {
            *lat = wcstod(val, NULL);
        } else if (!wcscmp(line, L"lon")) {
            *lon = wcstod(val, NULL);
        }
    }
    free(w);
}

void config_save(const wchar_t *city, double lat, double lon)
{
    wchar_t line[256];
    swprintf(line, 256, L"city=%s\nlat=%.4f\nlon=%.4f\n", city, lat, lon);

    char *u8 = w_to_u8(line);
    if (!u8) return;

    wchar_t dir[MAX_PATH + 32], path[MAX_PATH + 64];
    config_dir(dir, MAX_PATH + 32);
    swprintf(path, MAX_PATH + 64, L"%s\\config.txt", dir);

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD wr = 0;
        WriteFile(h, u8, (DWORD)strlen(u8), &wr, NULL);
        CloseHandle(h);
    }
    free(u8);
}
