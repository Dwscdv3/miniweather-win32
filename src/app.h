#ifndef APP_H
#define APP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define APP_TITLE   L"微天气"
#define APP_VERSION L"1.1.0"

/* 工作线程 -> UI 线程 */
#define WM_APP_WEATHER_OK  (WM_APP + 1)
#define WM_APP_WEATHER_ERR (WM_APP + 2)
#define WM_APP_COMMIT_CITY (WM_APP + 3)   /* 输入框回车 */

/* 暗色主题配色 */
#define C_BG     RGB(0x0E, 0x12, 0x17)
#define C_CARD   RGB(0x17, 0x1D, 0x25)
#define C_CARD2  RGB(0x24, 0x2E, 0x3A)   /* 选中标签 / 悬停 */
#define C_LINE   RGB(0x27, 0x30, 0x3B)
#define C_TEXT   RGB(0xEA, 0xEE, 0xF3)
#define C_MUTED  RGB(0x86, 0x93, 0xA3)
#define C_ACCENT RGB(0x54, 0xAE, 0xFF)
#define C_WARM   RGB(0xFF, 0x9E, 0x64)
#define C_ERR    RGB(0xFF, 0x7A, 0x7A)

enum { ST_LOADING = 0, ST_READY = 1, ST_ERROR = 2 };

#define HOURS_MAX  24
#define DAYS_MAX   7
#define MAX_CITIES 5

/* 窗口逻辑尺寸（96 DPI） */
#define WIN_W 460
#define WIN_H 590

typedef struct {
    wchar_t weekday[12];
    wchar_t date[12];
    int     code;
    double  tmin, tmax;
} DayForecast;

typedef struct {
    wchar_t city[96];
    wchar_t updated[24];
    double  lat, lon;
    double  temp, feels, wind, pressure, precip;
    int     humidity, code, is_day, wind_dir;
    int     hstart;                 /* 起始本地小时 */
    int     hcount;
    double  htemp[HOURS_MAX];
    int     hpop[HOURS_MAX];
    int     dcount;
    DayForecast days[DAYS_MAX];
} Weather;

typedef struct {
    int     mode;                   /* 0 = 按坐标取天气，1 = 城市搜索 */
    double  lat, lon;
    wchar_t city[96];
    wchar_t query[96];
} Job;

/* 一个常用城市；full 形如 "杭州 · 浙江" */
typedef struct {
    wchar_t full[96];
    double  lat, lon;
} City;

typedef struct {
    City     cities[MAX_CITIES];
    int      count;
    int      active;

    Weather *w;
    int      state;
    wchar_t  error[192];
    int      busy;

    /* 鼠标悬停（-1 表示无） */
    int      hover_tab;
    int      hover_close;
    int      hover_add;
    int      hover_refresh;
    int      drag_idx;              /* 正在拖拽排序的标签（-1 表示无） */
} AppState;

extern AppState g_app;
extern int      g_dpi;

/* net.c */
char *http_get(const wchar_t *url, size_t *out_len, wchar_t *err, int errcap);
void  http_free(char *p);

/* api.c */
Weather       *weather_fetch(const Job *job, wchar_t *err, int errcap);
void           weather_free(Weather *w);
const wchar_t *wmo_text(int code);
const wchar_t *wind_dir_text(int deg);

/* util.c */
wchar_t *u8_to_w(const char *s, int n);
char    *w_to_u8(const wchar_t *s);
void     trim_w(wchar_t *s);
void     city_short(const wchar_t *full, wchar_t *out, int cap);

/* store.c —— 常用城市持久化（注册表 HKCU\Software\MiniWeather） */
int  store_load(City *cities, int max, int *count, wchar_t *active, int cap);
void store_save(const City *cities, int count, const wchar_t *active);
int  legacy_load(wchar_t *city, int cap, double *lat, double *lon);

#endif
