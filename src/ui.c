/* 全部界面绘制：纯 GDI，双缓冲，无第三方依赖 */

#include "ui.h"

#include <math.h>
#include <stdio.h>

#define SZ(x) MulDiv((x), g_dpi, 96)
#define PI_ 3.14159265358979323846

static HFONT f_city, f_cond, f_body, f_small, f_tiny, f_big, f_pill;
static HBRUSH br_bg_edit;

static HDC     g_mem;
static HBITMAP g_bmp;
static HGDIOBJ g_oldbmp;
static int     g_mw, g_mh;

/* ---------------- 基础图元 ---------------- */

static HFONT mkfont(const wchar_t *face, int px, int weight)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight  = -SZ(px);
    lf.lfWeight  = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
    wcsncpy(lf.lfFaceName, face, 31);
    return CreateFontIndirectW(&lf);
}

static void font_drop(HFONT *f)
{
    if (*f) { DeleteObject(*f); *f = NULL; }
}

void ui_fonts_recreate(void)
{
    font_drop(&f_city);
    font_drop(&f_cond);
    font_drop(&f_body);
    font_drop(&f_small);
    font_drop(&f_tiny);
    font_drop(&f_big);
    font_drop(&f_pill);

    f_city  = mkfont(L"Microsoft YaHei UI", 17, FW_SEMIBOLD);
    f_cond  = mkfont(L"Microsoft YaHei UI", 15, FW_SEMIBOLD);
    f_body  = mkfont(L"Microsoft YaHei UI", 13, FW_NORMAL);
    f_small = mkfont(L"Microsoft YaHei UI", 11, FW_NORMAL);
    f_tiny  = mkfont(L"Microsoft YaHei UI", 10, FW_NORMAL);
    f_pill  = mkfont(L"Microsoft YaHei UI", 15, FW_SEMIBOLD);
    f_big   = mkfont(L"Segoe UI Light", 60, FW_NORMAL);
}

void ui_init(void)
{
    ui_fonts_recreate();
    br_bg_edit = CreateSolidBrush(C_CARD);
}

void ui_free(void)
{
    font_drop(&f_city); font_drop(&f_cond); font_drop(&f_body);
    font_drop(&f_small); font_drop(&f_tiny); font_drop(&f_big); font_drop(&f_pill);
    if (br_bg_edit) { DeleteObject(br_bg_edit); br_bg_edit = NULL; }
    if (g_mem) { SelectObject(g_mem, g_oldbmp); DeleteDC(g_mem); g_mem = NULL; }
    if (g_bmp) { DeleteObject(g_bmp); g_bmp = NULL; }
    g_mw = g_mh = 0;
}

HBRUSH ui_edit_brush(void) { return br_bg_edit; }
HFONT  ui_edit_font(void)  { return f_city; }

static void text(HDC dc, const wchar_t *s, RECT r, HFONT f, COLORREF c, UINT fmt)
{
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
    SelectObject(dc, of);
}

static void fill_rr(HDC dc, RECT r, int rad, COLORREF c)
{
    if (r.right <= r.left || r.bottom <= r.top) return;
    HBRUSH br = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad * 2, rad * 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

static void fill_circle(HDC dc, int cx, int cy, int r, COLORREF c)
{
    if (r <= 0) return;
    HBRUSH br = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

/* ---------------- 天气图标 ---------------- */

typedef struct { int x, y, w, h; } IBox;

static void sun_px(HDC dc, int cx, int cy, int r, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, (r / 4 > 0 ? r / 4 : 1), c);
    HGDIOBJ op = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));

    for (int i = 0; i < 8; i++) {
        double a = i * PI_ / 4.0;
        int x1 = cx + (int)lround(cos(a) * r * 1.45);
        int y1 = cy + (int)lround(sin(a) * r * 1.45);
        int x2 = cx + (int)lround(cos(a) * r * 2.05);
        int y2 = cy + (int)lround(sin(a) * r * 2.05);
        MoveToEx(dc, x1, y1, NULL);
        LineTo(dc, x2, y2);
    }
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);

    fill_circle(dc, cx, cy, r, c);
}

static void moon_px(HDC dc, int cx, int cy, int r, COLORREF c, COLORREF bg)
{
    fill_circle(dc, cx, cy, r, c);
    fill_circle(dc, cx + (int)(r * 0.42), cy - (int)(r * 0.22), (int)(r * 0.92), bg);
}

static void cloud_px(HDC dc, int cx, int cy, int w, COLORREF c)
{
    int r1 = (int)(w * 0.24), r2 = (int)(w * 0.28), r3 = (int)(w * 0.20);
    fill_circle(dc, cx - (int)(w * 0.22), cy, r1, c);
    fill_circle(dc, cx + (int)(w * 0.02), cy - (int)(w * 0.09), r2, c);
    fill_circle(dc, cx + (int)(w * 0.26), cy + (int)(w * 0.02), r3, c);

    RECT base;
    base.left   = cx - (int)(w * 0.44);
    base.right  = cx + (int)(w * 0.44);
    base.top    = cy - (int)(w * 0.02);
    base.bottom = cy + (int)(w * 0.20);
    fill_rr(dc, base, (int)(w * 0.11), c);
}

static void rain_px(HDC dc, int cx, int cy, int w, int snow, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, (w / 22 > 0 ? w / 22 : 1), c);
    HGDIOBJ op = SelectObject(dc, pen);

    if (snow) {
        SelectObject(dc, op);
        DeleteObject(pen);
        for (int i = 0; i < 3; i++) {
            int x = cx + (int)((i - 1) * w * 0.24);
            int y = cy + (int)(w * (0.30 + (i == 1 ? 0.10 : 0.02)));
            fill_circle(dc, x, y, (int)(w * 0.045) + 1, c);
        }
        return;
    }

    for (int i = 0; i < 3; i++) {
        int x = cx + (int)((i - 1) * w * 0.24);
        int y = cy + (int)(w * (0.26 + (i == 1 ? 0.06 : 0)));
        MoveToEx(dc, x, y, NULL);
        LineTo(dc, x - (int)(w * 0.06), y + (int)(w * 0.16));
    }
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void bolt_px(HDC dc, int cx, int cy, int w, COLORREF c)
{
    const double u[7][2] = {
        { 0.02, 0.16 }, { -0.14, 0.42 }, { -0.02, 0.42 }, { -0.10, 0.66 },
        { 0.14, 0.34 }, { 0.02, 0.34 }, { 0.06, 0.16 }
    };
    POINT p[7];
    for (int i = 0; i < 7; i++) {
        p[i].x = cx + (int)lround(u[i][0] * w);
        p[i].y = cy + (int)lround(u[i][1] * w);
    }
    HBRUSH br = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, p, 7);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
}

static void fog_px(HDC dc, int cx, int cy, int w, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, (w / 16 > 1 ? w / 16 : 1), c);
    HGDIOBJ op = SelectObject(dc, pen);
    for (int i = 0; i < 3; i++) {
        int half = (int)(w * (i == 1 ? 0.34 : 0.24));
        int y = cy + (int)((i - 1) * w * 0.20);
        MoveToEx(dc, cx - half, y, NULL);
        LineTo(dc, cx + half, y);
    }
    SelectObject(dc, op);
    DeleteObject(pen);
}

static void draw_icon(HDC dc, IBox b, int code, int isday, COLORREF bg)
{
    int s   = b.w;
    int cx  = b.x + s / 2;
    int cy  = b.y + s / 2;
    int clw = (int)(s * 0.78);
    COLORREF sunc = isday ? C_WARM : RGB(0xD8, 0xE1, 0xEC);
    COLORREF clc  = RGB(0xAF, 0xBE, 0xD0);
    COLORREF drop = RGB(0x6C, 0xC0, 0xF5);
    COLORREF snow = RGB(0xDC, 0xE8, 0xF6);

    switch (code) {
    case 0:
        if (isday) sun_px(dc, cx, cy, (int)(s * 0.19), sunc);
        else       moon_px(dc, cx, cy, (int)(s * 0.28), sunc, bg);
        break;

    case 1:
    case 2:
        if (isday) sun_px(dc, cx - (int)(s * 0.12), cy - (int)(s * 0.14), (int)(s * 0.13), sunc);
        else       moon_px(dc, cx - (int)(s * 0.14), cy - (int)(s * 0.16), (int)(s * 0.18), sunc, bg);
        cloud_px(dc, cx + (int)(s * 0.04), cy + (int)(s * 0.16), clw, clc);
        break;

    case 3:
        cloud_px(dc, cx, cy + (int)(s * 0.02), clw, clc);
        break;

    case 45:
    case 48:
        fog_px(dc, cx, cy, s, clc);
        break;

    case 51: case 53: case 55: case 56: case 57:
        cloud_px(dc, cx, cy - (int)(s * 0.12), (int)(s * 0.70), clc);
        rain_px(dc, cx, cy + (int)(s * 0.10), (int)(s * 0.62), 0, drop);
        break;

    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82:
        cloud_px(dc, cx, cy - (int)(s * 0.14), clw, clc);
        rain_px(dc, cx, cy + (int)(s * 0.08), (int)(s * 0.66), 0, drop);
        break;

    case 71: case 73: case 75: case 77: case 85: case 86:
        cloud_px(dc, cx, cy - (int)(s * 0.14), clw, clc);
        rain_px(dc, cx, cy + (int)(s * 0.08), (int)(s * 0.66), 1, snow);
        break;

    case 95: case 96: case 99:
        cloud_px(dc, cx, cy - (int)(s * 0.16), clw, clc);
        bolt_px(dc, cx, cy + (int)(s * 0.10), (int)(s * 0.62), C_WARM);
        break;

    default:
        cloud_px(dc, cx, cy, clw, clc);
        break;
    }
}

/* ---------------- 组件 ---------------- */

/* 温度色阶：低温偏蓝、高温偏橙红（类似气象图里的渐变） */
static COLORREF temp_color(double t)
{
    static const struct { double t; int r, g, b; } S[] = {
        { -10.0, 0x54, 0x8C, 0xFF },
        {   0.0, 0x54, 0xC8, 0xFF },
        {  10.0, 0x5F, 0xD3, 0x9B },
        {  20.0, 0xF0, 0xCE, 0x6A },
        {  30.0, 0xFF, 0x9E, 0x64 },
        {  40.0, 0xFF, 0x6B, 0x6B }
    };
    const int n = (int)(sizeof(S) / sizeof(S[0]));

    if (t <= S[0].t)     return RGB(S[0].r, S[0].g, S[0].b);
    if (t >= S[n - 1].t) return RGB(S[n - 1].r, S[n - 1].g, S[n - 1].b);

    for (int i = 1; i < n; i++) {
        if (t <= S[i].t) {
            double k = (t - S[i - 1].t) / (S[i].t - S[i - 1].t);
            int r = S[i - 1].r + (int)lround((S[i].r - S[i - 1].r) * k);
            int g = S[i - 1].g + (int)lround((S[i].g - S[i - 1].g) * k);
            int b = S[i - 1].b + (int)lround((S[i].b - S[i - 1].b) * k);
            return RGB(r, g, b);
        }
    }
    return C_ACCENT;
}

static void draw_hourly(HDC dc, RECT r)
{
    const Weather *w = g_app.w;
    int n = w ? w->hcount : 0;
    if (n < 2) return;

    double mn = w->htemp[0], mx = w->htemp[0];
    for (int i = 1; i < n; i++) {
        if (w->htemp[i] < mn) mn = w->htemp[i];
        if (w->htemp[i] > mx) mx = w->htemp[i];
    }
    if (mx - mn < 3.0) {
        double c = (mx + mn) * 0.5;
        mn = c - 1.5;
        mx = c + 1.5;
    }
    double pad = (mx - mn) * 0.20;
    mn -= pad;
    mx += pad;

    int step = (r.right - r.left) / (n - 1);
    if (step < 1) step = 1;

    POINT pts[HOURS_MAX];
    int top = r.top + SZ(4);
    int bot = r.bottom - SZ(4);
    for (int i = 0; i < n; i++) {
        double k = (w->htemp[i] - mn) / (mx - mn);
        pts[i].x = r.left + step * i;
        pts[i].y = bot - (LONG)lround(k * (bot - top));
    }

    /* 曲线下方淡色填充 */
    POINT poly[HOURS_MAX + 2];
    for (int i = 0; i < n; i++) poly[i] = pts[i];
    poly[n].x = pts[n - 1].x;
    poly[n].y = bot + SZ(2);
    poly[n + 1].x = pts[0].x;
    poly[n + 1].y = bot + SZ(2);

    HBRUSH ab = CreateSolidBrush(RGB(0x17, 0x2B, 0x3E));
    HGDIOBJ ob = SelectObject(dc, ab);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, poly, n + 2);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(ab);

    /* 降水概率柱（画在曲线之下、面积之上） */
    int bw = step / 2;
    if (bw < SZ(2)) bw = SZ(2);
    for (int i = 0; i < n; i++) {
        int pop = w->hpop[i];
        if (pop < 5) continue;

        int hgt = (int)lround((bot - top) * (pop / 100.0));
        if (hgt < SZ(2)) hgt = SZ(2);

        RECT pr;
        pr.left   = pts[i].x - bw / 2;
        pr.right  = pr.left + bw;
        pr.top    = bot + SZ(2) - hgt;
        pr.bottom = bot + SZ(2);
        fill_rr(dc, pr, bw / 2, RGB(0x2A, 0x4E, 0x6E));
    }

    /* 折线 */
    HPEN pen = CreatePen(PS_SOLID, SZ(2), C_ACCENT);
    HGDIOBJ opn = SelectObject(dc, pen);
    HGDIOBJ obr = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Polyline(dc, pts, n);
    SelectObject(dc, opn);
    SelectObject(dc, obr);
    DeleteObject(pen);

    /* 节点 + 时间刻度 */
    for (int i = 0; i < n; i += 3) {
        fill_circle(dc, pts[i].x, pts[i].y, SZ(2), C_ACCENT);

        wchar_t h[16];
        swprintf(h, 16, L"%d时", (w->hstart + i) % 24);
        RECT tr;
        tr.left   = pts[i].x - SZ(20);
        tr.right  = pts[i].x + SZ(20);
        tr.top    = r.bottom - SZ(1);
        tr.bottom = r.bottom + SZ(13);
        if (tr.left < r.left) { tr.left = pts[i].x; tr.right = pts[i].x + SZ(40); }
        if (tr.right > r.right) { tr.left = pts[i].x - SZ(40); tr.right = pts[i].x; }
        text(dc, h, tr, f_tiny, C_MUTED, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }

    /* 当前点标记 */
    fill_circle(dc, pts[0].x, pts[0].y, SZ(4), C_TEXT);
    fill_circle(dc, pts[0].x, pts[0].y, SZ(2), C_ACCENT);
}

static void draw_daily(HDC dc, RECT card)
{
    const Weather *w = g_app.w;
    int n = w ? w->dcount : 0;
    if (n <= 0) return;

    double mn = w->days[0].tmin, mx = w->days[0].tmax;
    for (int i = 1; i < n; i++) {
        if (w->days[i].tmin < mn) mn = w->days[i].tmin;
        if (w->days[i].tmax > mx) mx = w->days[i].tmax;
    }
    if (mx - mn < 6.0) { mn -= 2; mx += 2; }

    int left   = card.left + SZ(14);
    int bar_l  = left + SZ(166);
    int bar_r  = card.right - SZ(14) - SZ(42);
    int row_h  = SZ(26);

    for (int i = 0; i < n; i++) {
        const DayForecast *d = &w->days[i];
        int ry = card.top + SZ(8) + row_h * i;

        RECT rw;
        rw.left = left; rw.right = left + SZ(52);
        rw.top = ry; rw.bottom = ry + row_h;
        text(dc, i == 0 ? L"今天" : d->weekday, rw, f_body,
             i == 0 ? C_TEXT : C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT rd;
        rd.left = left + SZ(54); rd.right = left + SZ(96);
        rd.top = ry; rd.bottom = ry + row_h;
        text(dc, d->date, rd, f_tiny, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        IBox ib;
        ib.w = ib.h = SZ(20);
        ib.x = left + SZ(100);
        ib.y = ry + (row_h - ib.h) / 2;
        draw_icon(dc, ib, d->code, 1, C_CARD);

        wchar_t s[16];
        swprintf(s, 16, L"%.0f°", d->tmin);
        RECT rt1;
        rt1.left = left + SZ(120); rt1.right = left + SZ(158);
        rt1.top = ry; rt1.bottom = ry + row_h;
        text(dc, s, rt1, f_small, C_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        int ty = ry + row_h / 2;
        RECT track;
        track.left = bar_l; track.right = bar_r;
        track.top = ty - SZ(2); track.bottom = ty + SZ(2);
        fill_rr(dc, track, SZ(2), C_LINE);

        double span = (mx - mn) > 0.001 ? (mx - mn) : 1.0;
        int x1 = bar_l + (int)lround((d->tmin - mn) / span * (bar_r - bar_l));
        int x2 = bar_l + (int)lround((d->tmax - mn) / span * (bar_r - bar_l));
        if (x2 - x1 < SZ(8)) x2 = x1 + SZ(8);
        if (x2 > bar_r) x2 = bar_r;
        if (x1 < bar_l) x1 = bar_l;

        RECT seg;
        seg.left = x1; seg.right = x2;
        seg.top = ty - SZ(3); seg.bottom = ty + SZ(3);
        fill_rr(dc, seg, SZ(3), temp_color(d->tmax));

        swprintf(s, 16, L"%.0f°", d->tmax);
        RECT rt2;
        rt2.left = bar_r + SZ(8); rt2.right = card.right - SZ(14);
        rt2.top = ry; rt2.bottom = ry + row_h;
        text(dc, s, rt2, f_small, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void draw_stat(HDC dc, RECT r, const wchar_t *label, const wchar_t *value)
{
    fill_rr(dc, r, SZ(12), C_CARD);

    RECT lr;
    lr.left = r.left + SZ(12); lr.right = r.right - SZ(8);
    lr.top = r.top + SZ(9); lr.bottom = r.top + SZ(24);
    text(dc, label, lr, f_tiny, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT vr;
    vr.left = r.left + SZ(12); vr.right = r.right - SZ(8);
    vr.top = r.top + SZ(25); vr.bottom = r.bottom - SZ(6);
    text(dc, value, vr, f_pill, C_TEXT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* ---------------- 主绘制 ---------------- */

static void ensure_buffer(HDC ref, int w, int h)
{
    if (g_mem && g_mw == w && g_mh == h && g_bmp) return;

    if (g_mem) {
        SelectObject(g_mem, g_oldbmp);
        DeleteDC(g_mem);
        g_mem = NULL;
    }
    if (g_bmp) {
        DeleteObject(g_bmp);
        g_bmp = NULL;
    }
    g_mem = CreateCompatibleDC(ref);
    g_bmp = CreateCompatibleBitmap(ref, w, h);
    if (!g_mem || !g_bmp) return;
    g_oldbmp = SelectObject(g_mem, g_bmp);
    g_mw = w;
    g_mh = h;
}

static void center_note(HDC dc, int cw, int ch, const wchar_t *a, const wchar_t *b)
{
    RECT r;
    r.left = SZ(24); r.right = cw - SZ(24);
    r.top = ch / 2 - SZ(34); r.bottom = ch / 2 + SZ(4);
    text(dc, a, r, f_cond, C_MUTED, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (b) {
        RECT r2;
        r2.left = r.left; r2.right = r.right;
        r2.top = r.bottom + SZ(8); r2.bottom = r2.top + SZ(20);
        text(dc, b, r2, f_small, C_MUTED, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }
}

void ui_paint(HDC target, int cw, int ch)
{
    ensure_buffer(target, cw, ch);
    if (!g_mem || !g_bmp) return;

    HDC dc = g_mem;

    RECT full;
    full.left = 0; full.top = 0; full.right = cw; full.bottom = ch;
    HBRUSH bb = CreateSolidBrush(C_BG);
    FillRect(dc, &full, bb);
    DeleteObject(bb);

    const int PAD = SZ(20);
    int x = PAD;
    int w = cw - PAD * 2;

    /* ---- 城市行 ---- */
    int y = SZ(16);
    RECT rh;
    rh.left = x; rh.right = x + w - SZ(126);
    rh.top = y; rh.bottom = y + SZ(24);

    const Weather *wd = g_app.w;
    const wchar_t *cname = wd ? wd->city : (g_app.city[0] ? g_app.city : L"正在定位");
    text(dc, cname, rh, f_city, C_TEXT,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    wchar_t info[96] = L"";
    COLORREF infoc = C_MUTED;
    if (g_app.state == ST_ERROR && wd) {
        swprintf(info, 96, L"%s", g_app.error);      /* 有旧数据时只在右上角提示 */
        infoc = C_ERR;
    } else if (g_app.state == ST_LOADING) {
        swprintf(info, 96, L"正在更新…");
    } else if (wd) {
        swprintf(info, 96, L"更新于 %s", wd->updated);
    }

    RECT ri;
    ri.left = x + w - SZ(220); ri.right = x + w;
    ri.top = y; ri.bottom = y + SZ(24);
    text(dc, info, ri, f_small, infoc,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    y += SZ(24) + SZ(8);

    if (!wd) {
        if (g_app.state == ST_ERROR)
            center_note(dc, cw, ch, g_app.error, L"按 F5 重试，或按 C 换一个城市");
        else
            center_note(dc, cw, ch, L"正在获取天气…", L"首次启动需要联网");
        BitBlt(target, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
        return;
    }

    /* ---- 主温度 ---- */
    int hero   = SZ(122);
    int iconw  = SZ(92);

    RECT rt;
    rt.left = x; rt.right = x + w - iconw;
    rt.top = y; rt.bottom = y + SZ(76);
    wchar_t buf[96];
    swprintf(buf, 96, L"%.0f°", wd->temp);
    text(dc, buf, rt, f_big, C_TEXT, DT_LEFT | DT_TOP | DT_SINGLELINE);

    RECT rc;
    rc.left = x; rc.right = x + w - iconw;
    rc.top = y + SZ(72); rc.bottom = y + SZ(72) + SZ(22);
    text(dc, wmo_text(wd->code), rc, f_cond, C_TEXT,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT rf;
    rf.left = x; rf.right = x + w;
    rf.top = y + SZ(98); rf.bottom = y + SZ(98) + SZ(20);
    if (wd->dcount > 0)
        swprintf(buf, 96, L"体感 %.0f°    最高 %.0f° / 最低 %.0f°",
                 wd->feels, wd->days[0].tmax, wd->days[0].tmin);
    else
        swprintf(buf, 96, L"体感 %.0f°", wd->feels);
    text(dc, buf, rf, f_small, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    IBox hb;
    hb.w = hb.h = iconw;
    hb.x = x + w - iconw;
    hb.y = y + SZ(2);
    draw_icon(dc, hb, wd->code, wd->is_day, C_BG);

    y += hero;

    /* ---- 三项指标 ---- */
    int gap = SZ(10);
    int pw  = (w - gap * 2) / 3;
    for (int i = 0; i < 3; i++) {
        RECT p;
        p.left = x + i * (pw + gap);
        p.right = p.left + pw;
        p.top = y; p.bottom = y + SZ(54);

        wchar_t lbl[32], val[32];
        if (i == 0) {
            wcscpy(lbl, L"相对湿度");
            swprintf(val, 32, L"%d%%", wd->humidity);
        } else if (i == 1) {
            swprintf(lbl, 32, L"风速 · %s", wind_dir_text(wd->wind_dir));
            swprintf(val, 32, L"%.0f km/h", wd->wind);
        } else {
            wcscpy(lbl, L"气压");
            swprintf(val, 32, L"%.0f hPa", wd->pressure);
        }
        draw_stat(dc, p, lbl, val);
    }
    y += SZ(54) + SZ(12);

    /* ---- 24 小时 ---- */
    RECT hcard;
    hcard.left = x; hcard.right = x + w;
    hcard.top = y; hcard.bottom = y + SZ(100);
    fill_rr(dc, hcard, SZ(14), C_CARD);

    RECT htitle;
    htitle.left = hcard.left + SZ(12); htitle.right = hcard.right - SZ(12);
    htitle.top = hcard.top + SZ(8); htitle.bottom = hcard.top + SZ(22);
    text(dc, L"未来 24 小时", htitle, f_tiny, C_MUTED,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (wd->hcount > 1) {
        RECT hplot;
        hplot.left = hcard.left + SZ(14);
        hplot.right = hcard.right - SZ(14);
        hplot.top = hcard.top + SZ(26);
        hplot.bottom = hcard.bottom - SZ(18);
        draw_hourly(dc, hplot);
    }
    y += SZ(100) + SZ(12);

    /* ---- 7 天 ---- */
    if (wd->dcount > 0) {
        RECT dcard;
        dcard.left = x; dcard.right = x + w;
        dcard.top = y;
        dcard.bottom = y + SZ(16) + SZ(26) * wd->dcount;
        fill_rr(dc, dcard, SZ(14), C_CARD);
        draw_daily(dc, dcard);
    }

    /* ---- 底栏 ---- */
    RECT ft;
    ft.left = x; ft.right = x + w;
    ft.top = ch - SZ(26); ft.bottom = ch - SZ(12);
    text(dc, L"F5 刷新   ·   C 换城市   ·   数据 Open-Meteo", ft, f_tiny, C_MUTED,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    BitBlt(target, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
}

RECT ui_city_rect(int cw)
{
    RECT r;
    r.left = SZ(14);
    r.right = cw - SZ(136);
    r.top = SZ(12);
    r.bottom = SZ(12) + SZ(32);
    return r;
}
