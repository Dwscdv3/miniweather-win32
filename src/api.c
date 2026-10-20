/* Open-Meteo 数据接入：天气查询、城市地理编码、天气码文案 */

#include "app.h"
#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#define FORECAST_URL \
    L"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f" \
    L"&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day," \
    L"weather_code,wind_speed_10m,wind_direction_10m,pressure_msl,precipitation," \
    L"visibility,cloud_cover" \
    L"&hourly=temperature_2m,precipitation_probability,precipitation" \
    L"&daily=weather_code,temperature_2m_max,temperature_2m_min,sunrise,sunset" \
    L"&timezone=auto&forecast_days=7&wind_speed_unit=kmh"

static void seterr(wchar_t *err, int cap, const wchar_t *fmt, ...)
{
    if (!err || cap <= 0) return;
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(err, (size_t)cap - 1, fmt, ap);
    err[cap - 1] = 0;
    va_end(ap);
}

const wchar_t *wmo_text(int code)
{
    switch (code) {
    case 0:  return L"晴";
    case 1:  return L"晴间多云";
    case 2:  return L"多云";
    case 3:  return L"阴";
    case 45: return L"有雾";
    case 48: return L"霜雾";
    case 51: return L"毛毛雨";
    case 53: return L"毛毛雨";
    case 55: return L"浓毛毛雨";
    case 56: case 57: return L"冻毛毛雨";
    case 61: return L"小雨";
    case 63: return L"中雨";
    case 65: return L"大雨";
    case 66: case 67: return L"冻雨";
    case 71: return L"小雪";
    case 73: return L"中雪";
    case 75: return L"大雪";
    case 77: return L"米雪";
    case 80: return L"阵雨";
    case 81: return L"强阵雨";
    case 82: return L"暴雨";
    case 85: return L"阵雪";
    case 86: return L"强阵雪";
    case 95: return L"雷阵雨";
    case 96: case 99: return L"雷阵雨伴冰雹";
    default: return L"未知";
    }
}

const wchar_t *wind_dir_text(int deg)
{
    static const wchar_t *D[8] = {
        L"北风", L"东北风", L"东风", L"东南风",
        L"南风", L"西南风", L"西风", L"西北风"
    };
    if (deg < 0) deg += 360;
    int idx = ((deg + 22) / 45) % 8;
    if (idx < 0) idx += 8;
    return D[idx];
}

static int geocode(const wchar_t *q, double *lat, double *lon,
                   wchar_t *city, int cap, wchar_t *err, int errcap)
{
    char *u8 = w_to_u8(q);
    if (!u8) { seterr(err, errcap, L"城市名编码失败"); return 0; }

    char enc[512];
    size_t k = 0;
    for (const char *c = u8; *c && k + 5 < sizeof(enc); c++) {
        unsigned char ch = (unsigned char)*c;
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            enc[k++] = (char)ch;
        } else {
            sprintf(enc + k, "%%%02X", ch);
            k += 3;
        }
    }
    enc[k] = 0;
    free(u8);

    wchar_t wenc[600];
    for (size_t i = 0; i <= k; i++) wenc[i] = (wchar_t)(unsigned char)enc[i];

    wchar_t url[1024];
    swprintf(url, 1024,
             L"https://geocoding-api.open-meteo.com/v1/search?name=%s&count=1&language=zh&format=json",
             wenc);

    size_t len = 0;
    char *raw = http_get(url, &len, err, errcap);
    if (!raw) return 0;

    JNode *root = json_parse(raw, len);
    http_free(raw);
    if (!root) { seterr(err, errcap, L"城市数据解析失败"); return 0; }

    JNode *first = json_at(json_obj(root, L"results"), 0);
    if (!first) {
        seterr(err, errcap, L"未找到城市「%s」", q);
        json_free(root);
        return 0;
    }

    *lat = json_num(json_obj(first, L"latitude"), 0);
    *lon = json_num(json_obj(first, L"longitude"), 0);
    const wchar_t *nm  = json_str(json_obj(first, L"name"), q);
    const wchar_t *adm = json_str(json_obj(first, L"admin1"), L"");
    if (adm[0] && wcscmp(adm, nm))
        swprintf(city, (size_t)cap, L"%s · %s", nm, adm);
    else
        swprintf(city, (size_t)cap, L"%s", nm);

    json_free(root);
    return 1;
}

static Weather *openmeteo_fetch(const Job *job, wchar_t *err, int errcap)
{
    double lat = job->lat, lon = job->lon;
    wchar_t city[96];
    wcsncpy(city, job->city, 95);
    city[95] = 0;

    if (job->mode == 1) {
        if (!geocode(job->query, &lat, &lon, city, 96, err, errcap)) return NULL;
    }

    wchar_t url[1024];
    swprintf(url, 1024, FORECAST_URL, lat, lon);

    size_t len = 0;
    char *raw = http_get(url, &len, err, errcap);
    if (!raw) return NULL;

    JNode *root = json_parse(raw, len);
    http_free(raw);
    if (!root) { seterr(err, errcap, L"天气数据解析失败"); return NULL; }

    Weather *w = (Weather *)calloc(1, sizeof(Weather));
    if (!w) { json_free(root); seterr(err, errcap, L"内存不足"); return NULL; }

    w->lat = lat;
    w->lon = lon;
    wcsncpy(w->city, city, 95);

    JNode *cur = json_obj(root, L"current");
    w->temp     = json_num(json_obj(cur, L"temperature_2m"), 0);
    w->feels    = json_num(json_obj(cur, L"apparent_temperature"), w->temp);
    w->humidity = (int)json_num(json_obj(cur, L"relative_humidity_2m"), 0);
    w->code     = (int)json_num(json_obj(cur, L"weather_code"), 0);
    w->is_day   = (int)json_num(json_obj(cur, L"is_day"), 1);
    w->wind     = json_num(json_obj(cur, L"wind_speed_10m"), 0);
    w->wind_dir = (int)json_num(json_obj(cur, L"wind_direction_10m"), 0);
    w->pressure = json_num(json_obj(cur, L"pressure_msl"), 0);
    w->precip   = json_num(json_obj(cur, L"precipitation"), 0);
    w->vis      = json_num(json_obj(cur, L"visibility"), 0) / 1000.0;   /* 米 -> 公里 */
    w->cloud    = json_num(json_obj(cur, L"cloud_cover"), 0);

    const wchar_t *now = json_str(json_obj(cur, L"time"), L"");
    int curhour = time_hour(now);
    if (wcslen(now) >= 16)
        swprintf(w->updated, 24, L"%s", now + 11);
    else
        swprintf(w->updated, 24, L"--:--");

    wcsncpy(w->desc, wmo_text(w->code), 23);

    /* 逐小时：从当前小时开始取 24 条 */
    JNode *hl    = json_obj(root, L"hourly");
    JNode *htime = json_obj(hl, L"time");
    JNode *htemp = json_obj(hl, L"temperature_2m");
    JNode *hpop  = json_obj(hl, L"precipitation_probability");
    JNode *hpr   = json_obj(hl, L"precipitation");
    int hn = json_count(htime);

    int start = 0;
    for (int i = 0; i < hn; i++) {
        if (time_hour(json_str(json_at(htime, i), L"")) == curhour) { start = i; break; }
    }

    int c = 0;
    for (int i = start; i < hn && c < HOURS_MAX; i++, c++) {
        w->htemp[c]   = json_num(json_at(htemp, i), 0);
        w->hpop[c]    = (int)json_num(json_at(hpop, i), 0);
        w->hprecip[c] = json_num(json_at(hpr, i), 0);
        w->hhour[c]   = time_hour(json_str(json_at(htime, i), L""));
    }
    w->hcount = c;
    w->hstart = curhour;

    /* 逐日 */
    JNode *dl    = json_obj(root, L"daily");
    JNode *dtime = json_obj(dl, L"time");
    JNode *dcode = json_obj(dl, L"weather_code");
    JNode *dmax  = json_obj(dl, L"temperature_2m_max");
    JNode *dmin  = json_obj(dl, L"temperature_2m_min");
    JNode *dsr   = json_obj(dl, L"sunrise");
    JNode *dss   = json_obj(dl, L"sunset");
    int dn = json_count(dtime);

    for (int i = 0; i < dn && i < DAYS_MAX; i++) {
        const wchar_t *ds = json_str(json_at(dtime, i), L"");
        DayForecast *d = &w->days[i];
        weekday_text(ds, d->weekday, 12);
        date_label(ds, d->date, 12);
        d->code = (int)json_num(json_at(dcode, i), 0);
        d->tmax = json_num(json_at(dmax, i), 0);
        d->tmin = json_num(json_at(dmin, i), 0);

        if (i == 0) {
            /* "2026-10-21T06:30" -> "06:30" */
            const wchar_t *sr = json_str(json_at(dsr, 0), L"");
            const wchar_t *ss = json_str(json_at(dss, 0), L"");
            if (wcslen(sr) >= 16) swprintf(w->sunrise, 8, L"%.5s", sr + 11);
            if (wcslen(ss) >= 16) swprintf(w->sunset, 8, L"%.5s", ss + 11);
        }
        w->dcount++;
    }

    json_free(root);
    return w;
}

/* 统一入口：配好和风的 Host + Key 就走和风（中国区更准、支持区县），否则回退 Open-Meteo */
Weather *weather_fetch(const Job *job, wchar_t *err, int errcap)
{
    if (qweather_ready()) return qweather_fetch(job, err, errcap);
    return openmeteo_fetch(job, err, errcap);
}

void weather_free(Weather *w)
{
    free(w);
}
