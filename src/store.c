/* 常用城市持久化：HKCU\Software\MiniWeather
 *   Cities (REG_MULTI_SZ)  每项 "杭州 · 浙江|30.2936|120.1614"
 *   Active (REG_SZ)        当前选中城市全名
 * 同时负责从旧版 %APPDATA%\MiniWeather\config.txt 迁移一次。 */

#include "app.h"

#include <stdio.h>

#define REG_PATH   L"Software\\MiniWeather"
#define REG_CITIES L"Cities"
#define REG_ACTIVE L"Active"

int store_load(City *cities, int max, int *count, wchar_t *active, int cap)
{
    if (!cities || !count) return 0;
    *count = 0;
    if (active && cap > 0) active[0] = 0;

    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return 0;

    DWORD type = 0, size = 0;
    LONG rc = RegQueryValueExW(key, REG_CITIES, NULL, &type, NULL, &size);
    if (rc != ERROR_SUCCESS || type != REG_MULTI_SZ || size < sizeof(wchar_t)) {
        RegCloseKey(key);
        return 0;
    }

    wchar_t *buf = (wchar_t *)calloc(1, (size_t)size + sizeof(wchar_t) * 2);
    if (!buf) { RegCloseKey(key); return 0; }

    rc = RegQueryValueExW(key, REG_CITIES, NULL, &type, (BYTE *)buf, &size);
    if (rc != ERROR_SUCCESS) {
        free(buf);
        RegCloseKey(key);
        return 0;
    }

    int n = 0;
    for (wchar_t *p = buf; *p && n < max; ) {
        size_t elen = wcslen(p);

        wchar_t *sep1 = wcschr(p, L'|');
        if (sep1) {
            *sep1 = 0;
            wchar_t *sep2 = wcschr(sep1 + 1, L'|');
            if (sep2) {
                *sep2 = 0;
                double la = wcstod(sep1 + 1, NULL);
                double lo = wcstod(sep2 + 1, NULL);
                if (p[0] && la >= -90 && la <= 90 && lo >= -180 && lo <= 180) {
                    wcsncpy(cities[n].full, p, 95);
                    cities[n].full[95] = 0;
                    cities[n].lat = la;
                    cities[n].lon = lo;
                    n++;
                }
            }
        }
        p += elen + 1;
    }
    *count = n;

    if (active && cap > 0) {
        wchar_t act[96] = L"";
        DWORD sz = sizeof(act);
        if (RegQueryValueExW(key, REG_ACTIVE, NULL, &type, (BYTE *)act, &sz) == ERROR_SUCCESS &&
            type == REG_SZ) {
            act[95] = 0;
            wcsncpy(active, act, (size_t)cap - 1);
            active[cap - 1] = 0;
        }
    }

    RegCloseKey(key);
    free(buf);
    return n > 0;
}

void store_save(const City *cities, int count, const wchar_t *active)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 0,
                        KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS)
        return;

    if (count > 0) {
        size_t cap = 8;
        for (int i = 0; i < count; i++) cap += wcslen(cities[i].full) + 40;

        wchar_t *buf = (wchar_t *)calloc(cap, sizeof(wchar_t));
        if (buf) {
            size_t pos = 0;
            for (int i = 0; i < count; i++) {
                int n = swprintf(buf + pos, cap - pos, L"%s|%.4f|%.4f",
                                 cities[i].full, cities[i].lat, cities[i].lon);
                if (n <= 0) break;
                pos += (size_t)n + 1;      /* 跳过 swprintf 写入的结尾 0 */
            }
            buf[pos] = 0;                  /* 双 0 结尾 */

            RegSetValueExW(key, REG_CITIES, 0, REG_MULTI_SZ, (const BYTE *)buf,
                           (DWORD)((pos + 1) * sizeof(wchar_t)));
            free(buf);
        }
    }

    if (active && active[0]) {
        RegSetValueExW(key, REG_ACTIVE, 0, REG_SZ, (const BYTE *)active,
                       (DWORD)((wcslen(active) + 1) * sizeof(wchar_t)));
    }
    RegCloseKey(key);
}

/* ---- 旧版 config.txt 迁移 ---- */

static void legacy_path(wchar_t *out, int cap)
{
    wchar_t dir[MAX_PATH] = L"";
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        GetModuleFileNameW(NULL, dir, MAX_PATH);
        wchar_t *p = wcsrchr(dir, L'\\');
        if (p) *p = 0;
    }
    swprintf(out, (size_t)cap, L"%s\\MiniWeather\\config.txt", dir);
}

int legacy_load(wchar_t *city, int cap, double *lat, double *lon)
{
    if (!city || cap <= 0) return 0;

    wchar_t path[MAX_PATH + 64];
    legacy_path(path, MAX_PATH + 64);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    char buf[4096];
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);
    if (got == 0) return 0;
    buf[got] = 0;

    wchar_t *w = u8_to_w(buf, (int)got);
    if (!w) return 0;

    int found = 0;
    wchar_t name[96] = L"";
    double la = 0, lo = 0;

    wchar_t *ctx = NULL;
    for (wchar_t *line = wcstok(w, L"\r\n", &ctx); line; line = wcstok(NULL, L"\r\n", &ctx)) {
        wchar_t *eq = wcschr(line, L'=');
        if (!eq) continue;
        *eq = 0;
        trim_w(line);
        wchar_t *val = eq + 1;
        trim_w(val);

        if (!wcscmp(line, L"city") && val[0]) {
            wcsncpy(name, val, 95);
            name[95] = 0;
        } else if (!wcscmp(line, L"lat")) {
            la = wcstod(val, NULL);
        } else if (!wcscmp(line, L"lon")) {
            lo = wcstod(val, NULL);
        }
    }
    free(w);

    if (name[0] && la >= -90 && la <= 90 && lo >= -180 && lo <= 180) {
        wcsncpy(city, name, (size_t)cap - 1);
        city[cap - 1] = 0;
        *lat = la;
        *lon = lo;
        found = 1;
    }
    return found;
}
