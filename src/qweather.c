/* 和风天气（QWeather）数据接入
 * 鉴权：请求头 X-QW-Api-Key；域名必须是账号专属 API Host（形如 xxxxx.re.qweatherapi.com）
 * 单位：温度 ℃、风速 km/h、气压 hPa、降水 mm */

#include "app.h"
#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

static void seterr(wchar_t *err, int cap, const wchar_t *fmt, ...)
{
    if (!err || cap <= 0) return;
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(err, (size_t)cap - 1, fmt, ap);
    err[cap - 1] = 0;
    va_end(ap);
}

int qweather_ready(void)
{
    return g_api.host[0] != 0 && g_api.key[0] != 0;
}

/* 和风的字段全是字符串 */
static double jnum(const JNode *o, const wchar_t *k, double def)
{
    const JNode *v = json_obj(o, k);
    if (!v) return def;
    if (v->type == JS_STR && v->str) return wcstod(v->str, NULL);
    return json_num(v, def);
}

static int jint(const JNode *o, const wchar_t *k, int def)
{
    return (int)jnum(o, k, def);
}

/* 和风图标码 -> WMO 近似码，复用自绘图标 */
static int qw_icon_to_wmo(int icon)
{
    if (icon == 100 || icon == 150) return 0;                      /* 晴 */
    if (icon == 101 || icon == 102 || icon == 103) return 2;       /* 多云 / 少云 / 晴间多云 */
    if (icon == 104 || (icon >= 151 && icon <= 154)) return 3;     /* 阴 */

    if (icon == 302 || icon == 303) return 95;                     /* 雷阵雨 */
    if (icon == 304) return 96;                                    /* 雷阵雨伴冰雹 */
    if (icon == 300 || icon == 301) return 80;                     /* 阵雨 / 强阵雨 */

    if (icon == 309) return 51;                                    /* 毛毛雨 */
    if (icon == 313) return 66;                                    /* 冻雨 */
    if (icon == 305 || icon == 314) return 61;                     /* 小雨 */
    if (icon == 306 || icon == 315) return 63;                     /* 中雨 */
    if (icon >= 307 && icon <= 318) return 65;                     /* 大雨及以上 */
    if (icon == 399) return 63;

    if (icon == 404 || icon == 405 || icon == 406) return 66;      /* 雨夹雪 */
    if (icon == 400 || icon == 407 || icon == 408) return 71;      /* 小雪 / 阵雪 */
    if (icon == 401 || icon == 409) return 73;                     /* 中雪 */
    if (icon >= 402 && icon <= 410) return 75;                     /* 大雪及以上 */
    if (icon == 499) return 73;

    if (icon >= 500 && icon <= 515) return 45;                     /* 雾 / 霾 / 沙尘 */
    return 0;
}

static const wchar_t *qw_code_text(const wchar_t *code)
{
    if (!wcscmp(code, L"204")) return L"该地区暂无天气数据";
    if (!wcscmp(code, L"400")) return L"请求参数有误";
    if (!wcscmp(code, L"401")) return L"和风 API Key 无效";
    if (!wcscmp(code, L"402")) return L"和风账户额度不足";
    if (!wcscmp(code, L"403")) return L"和风 API Host 或权限不正确";
    if (!wcscmp(code, L"404")) return L"未找到该城市";
    if (!wcscmp(code, L"429")) return L"请求过于频繁，请稍后再试";
    if (!wcscmp(code, L"500")) return L"和风服务器错误";
    return L"和风接口返回异常";
}

static JNode *qw_call(const wchar_t *url, wchar_t *err, int errcap)
{
    wchar_t hdr[192];
    swprintf(hdr, 192, L"X-QW-Api-Key: %s\r\n", g_api.key);

    size_t len = 0;
    char *raw = http_get_ex(url, hdr, &len, err, errcap);
    if (!raw) return NULL;

    JNode *root = json_parse(raw, len);
    http_free(raw);
    if (!root) { seterr(err, errcap, L"和风数据解析失败"); return NULL; }

    const wchar_t *code = json_str(json_obj(root, L"code"), L"");
    if (wcscmp(code, L"200")) {
        /* 鉴权/域名错误时返回的是 {"error":{"status":403,"title":"Invalid Host"}} */
        const wchar_t *title = json_str(json_obj(json_obj(root, L"error"), L"title"), L"");
        if (title[0])
            seterr(err, errcap, L"和风接口错误：%s", title);
        else
            seterr(err, errcap, L"%s（%s）", qw_code_text(code), code[0] ? code : L"--");
        json_free(root);
        return NULL;
    }
    return root;
}

/* 城市名 -> LocationID；同时回填显示名与经纬度 */
static int qw_geocode(const wchar_t *q, wchar_t *loc, int loccap,
                      wchar_t *city, int citycap, double *lat, double *lon,
                      wchar_t *err, int errcap)
{
    char *u8 = w_to_u8(q);
    if (!u8) { seterr(err, errcap, L"城市名编码失败"); return 0; }

    char enc[512];
    size_t k = 0;
    for (const char *p = u8; *p && k + 5 < sizeof(enc); p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') enc[k++] = (char)c;
        else { sprintf(enc + k, "%%%02X", c); k += 3; }
    }
    enc[k] = 0;
    free(u8);

    wchar_t wenc[600];
    for (size_t i = 0; i <= k; i++) wenc[i] = (wchar_t)(unsigned char)enc[i];

    wchar_t url[900];
    swprintf(url, 900, L"https://%s/geo/v2/city/lookup?location=%s&number=1&lang=zh",
             g_api.host, wenc);

    JNode *root = qw_call(url, err, errcap);
    if (!root) return 0;

    const JNode *first = json_at(json_obj(root, L"location"), 0);
    const wchar_t *id = first ? json_str(json_obj(first, L"id"), L"") : L"";
    if (!id[0]) {
        seterr(err, errcap, L"未找到城市「%s」", q);
        json_free(root);
        return 0;
    }

    wcsncpy(loc, id, (size_t)loccap - 1);
    loc[loccap - 1] = 0;

    const wchar_t *nm = json_str(json_obj(first, L"name"), q);
    const wchar_t *a2 = json_str(json_obj(first, L"adm2"), L"");
    const wchar_t *a1 = json_str(json_obj(first, L"adm1"), L"");
    if (a2[0] && wcscmp(a2, nm))       swprintf(city, (size_t)citycap, L"%s · %s", nm, a2);
    else if (a1[0] && wcscmp(a1, nm))  swprintf(city, (size_t)citycap, L"%s · %s", nm, a1);
    else                               swprintf(city, (size_t)citycap, L"%s", nm);

    *lat = jnum(first, L"lat", 0);
    *lon = jnum(first, L"lon", 0);

    json_free(root);
    return 1;
}

/* 新版空气质量接口没有 code 字段，单独走一个不做状态码校验的入口 */
static JNode *qw_get_json(const wchar_t *url)
{
    wchar_t hdr[192];
    swprintf(hdr, 192, L"X-QW-Api-Key: %s\r\n", g_api.key);

    size_t len = 0;
    char *raw = http_get_ex(url, hdr, &len, NULL, 0);
    if (!raw) return NULL;

    JNode *root = json_parse(raw, len);
    http_free(raw);
    return root;
}

/* 空气质量：优先取中国标准（cn-mee），没有就取第一个 */
static void fill_air(Weather *w)
{
    wchar_t url[320];
    swprintf(url, 320, L"https://%s/airquality/v1/current/%.2f/%.2f?lang=zh",
             g_api.host, w->lat, w->lon);

    JNode *root = qw_get_json(url);
    if (!root) return;

    const JNode *arr = json_obj(root, L"indexes");
    const JNode *pick = NULL;
    int n = json_count(arr);
    for (int i = 0; i < n; i++) {
        const JNode *it = json_at(arr, i);
        const wchar_t *code = json_str(json_obj(it, L"code"), L"");
        if (!wcscmp(code, L"cn-mee")) { pick = it; break; }
    }
    if (!pick) pick = json_at(arr, 0);
    if (!pick) { json_free(root); return; }

    w->has_aqi   = 1;
    w->aqi       = jint(pick, L"aqi", 0);
    w->aqi_level = jint(pick, L"level", 0);

    const wchar_t *cat = json_str(json_obj(pick, L"category"), L"");
    wcsncpy(w->aqi_cat, cat, 15);
    w->aqi_cat[15] = 0;

    const JNode *col = json_obj(pick, L"color");
    if (col) {
        int r = jint(col, L"red", 0);
        int g = jint(col, L"green", 0);
        int b = jint(col, L"blue", 0);
        if (r || g || b) w->aqi_color = RGB(r, g, b);
    }

    json_free(root);
}

/* 分钟级降水（仅中国区，location 必须是 "经度,纬度"） */
static void fill_minutely(Weather *w)
{
    wchar_t url[320];
    swprintf(url, 320, L"https://%s/v7/minutely/5m?location=%.4f,%.4f&lang=zh",
             g_api.host, w->lon, w->lat);

    JNode *root = qw_call(url, NULL, 0);
    if (!root) return;

    const wchar_t *summary = json_str(json_obj(root, L"summary"), L"");
    if (summary[0]) {
        wcsncpy(w->minutely, summary, 47);
        w->minutely[47] = 0;
    }
    json_free(root);
}

Weather *qweather_fetch(const Job *job, wchar_t *err, int errcap)
{
    wchar_t loc[64] = L"";
    wchar_t city[96] = L"";
    double lat = job->lat;
    double lon = job->lon;

    if (job->mode == 1) {
        if (!qw_geocode(job->query, loc, 64, city, 96, &lat, &lon, err, errcap)) return NULL;
    } else {
        swprintf(loc, 64, L"%.4f,%.4f", lon, lat);
        wcsncpy(city, job->city, 95);
        city[95] = 0;
    }

    wchar_t url[640];

    /* ---- 实况 ---- */
    swprintf(url, 640, L"https://%s/v7/weather/now?location=%s&lang=zh", g_api.host, loc);
    JNode *now = qw_call(url, err, errcap);
    if (!now) return NULL;

    Weather *w = (Weather *)calloc(1, sizeof(Weather));
    if (!w) { json_free(now); seterr(err, errcap, L"内存不足"); return NULL; }

    wcsncpy(w->city, city, 95);
    w->lat = lat;
    w->lon = lon;

    const JNode *n = json_obj(now, L"now");
    w->temp     = jnum(n, L"temp", 0);
    w->feels    = jnum(n, L"feelsLike", w->temp);
    w->humidity = jint(n, L"humidity", 0);
    w->wind     = jnum(n, L"windSpeed", 0);
    w->wind_dir = jint(n, L"wind360", 0);
    w->pressure = jnum(n, L"pressure", 0);
    w->precip   = jnum(n, L"precip", 0);
    w->vis      = jnum(n, L"vis", 0);       /* 公里 */
    w->cloud    = jint(n, L"cloud", 0);     /* 百分比 */

    int icon = jint(n, L"icon", 100);
    w->code = qw_icon_to_wmo(icon);

    const wchar_t *txt = json_str(json_obj(n, L"text"), L"");
    wcsncpy(w->desc, txt[0] ? txt : wmo_text(w->code), 23);
    w->desc[23] = 0;

    const wchar_t *obs = json_str(json_obj(n, L"obsTime"), L"");
    int ohour = time_hour(obs);
    if (wcslen(obs) >= 16) swprintf(w->updated, 24, L"%.5s", obs + 11);   /* 只取 HH:MM */
    else                   swprintf(w->updated, 24, L"--:--");

    /* 150~154 是夜间图标；其余按观测小时判断昼夜 */
    if (icon >= 150 && icon <= 154)      w->is_day = 0;
    else if (ohour < 6 || ohour >= 19)   w->is_day = 0;
    else                                 w->is_day = 1;

    w->hstart = ohour;
    json_free(now);

    /* ---- 逐小时（24 条，从下一个整点开始） ---- */
    swprintf(url, 640, L"https://%s/v7/weather/24h?location=%s&lang=zh", g_api.host, loc);
    JNode *h = qw_call(url, err, errcap);
    if (h) {
        const JNode *arr = json_obj(h, L"hourly");
        int hn = json_count(arr);
        int c = 0;
        for (int i = 0; i < hn && c < HOURS_MAX; i++) {
            const JNode *it = json_at(arr, i);
            w->htemp[c] = jnum(it, L"temp", 0);
            w->hpop[c]  = jint(it, L"pop", 0);
            w->hprecip[c] = jnum(it, L"precip", 0);
            w->hhour[c] = time_hour(json_str(json_obj(it, L"fxTime"), L""));
            c++;
        }
        w->hcount = c;
        json_free(h);
    }

    /* ---- 7 天 ---- */
    swprintf(url, 640, L"https://%s/v7/weather/7d?location=%s&lang=zh", g_api.host, loc);
    JNode *d = qw_call(url, err, errcap);
    if (d) {
        const JNode *arr = json_obj(d, L"daily");
        int dn = json_count(arr);
        for (int i = 0; i < dn && i < DAYS_MAX; i++) {
            const JNode *it = json_at(arr, i);
            const wchar_t *ds = json_str(json_obj(it, L"fxDate"), L"");
            DayForecast *df = &w->days[i];
            weekday_text(ds, df->weekday, 12);
            date_label(ds, df->date, 12);
            df->tmax = jnum(it, L"tempMax", 0);
            df->tmin = jnum(it, L"tempMin", 0);
            df->code = qw_icon_to_wmo(jint(it, L"iconDay", 100));

            if (i == 0) {
                wcsncpy(w->sunrise, json_str(json_obj(it, L"sunrise"), L""), 7);
                wcsncpy(w->sunset,  json_str(json_obj(it, L"sunset"),  L""), 7);
                w->sunrise[7] = 0;
                w->sunset[7]  = 0;
            }
            w->dcount++;
        }
        json_free(d);
    }

    fill_air(w);        /* 以下是可选的附加数据，失败不影响主体 */
    fill_minutely(w);

    return w;
}
