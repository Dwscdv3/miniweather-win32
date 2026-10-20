/* 全部界面绘制：纯 GDI，双缓冲，无第三方依赖 */

#include "ui.h"

#include <math.h>
#include <stdio.h>

#define SZ(x) MulDiv((x), g_dpi, 96)
#define PI_ 3.14159265358979323846

static HFONT f_tab, f_cond, f_body, f_small, f_tiny, f_big, f_pill, f_icon;
static HBRUSH br_bg_edit;

static HDC     g_mem;      /* 后备缓冲 */
static HBITMAP g_bmp;
static HGDIOBJ g_oldbmp;
static int     g_mw, g_mh;

static HDC     g_mdc;      /* 文本测量用的内存 DC */

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
    font_drop(&f_tab);
    font_drop(&f_cond);
    font_drop(&f_body);
    font_drop(&f_small);
    font_drop(&f_tiny);
    font_drop(&f_big);
    font_drop(&f_pill);
    font_drop(&f_icon);

    f_tab   = mkfont(L"Microsoft YaHei UI", 12, FW_MEDIUM);
    f_cond  = mkfont(L"Microsoft YaHei UI", 15, FW_SEMIBOLD);
    f_body  = mkfont(L"Microsoft YaHei UI", 13, FW_NORMAL);
    f_small = mkfont(L"Microsoft YaHei UI", 11, FW_NORMAL);
    f_tiny  = mkfont(L"Microsoft YaHei UI", 10, FW_NORMAL);
    f_pill  = mkfont(L"Microsoft YaHei UI", 15, FW_SEMIBOLD);
    f_big   = mkfont(L"Segoe UI Light", 60, FW_NORMAL);
    f_icon  = mkfont(L"Segoe UI Symbol", 15, FW_NORMAL);
}

void ui_init(void)
{
    g_mdc = CreateCompatibleDC(NULL);
    ui_fonts_recreate();
    br_bg_edit = CreateSolidBrush(C_CARD);
}

void ui_free(void)
{
    font_drop(&f_tab); font_drop(&f_cond); font_drop(&f_body);
    font_drop(&f_small); font_drop(&f_tiny); font_drop(&f_big);
    font_drop(&f_pill); font_drop(&f_icon);

    if (br_bg_edit) { DeleteObject(br_bg_edit); br_bg_edit = NULL; }
    if (g_mem) { SelectObject(g_mem, g_oldbmp); DeleteDC(g_mem); g_mem = NULL; }
    if (g_bmp) { DeleteObject(g_bmp); g_bmp = NULL; }
    if (g_mdc) { DeleteDC(g_mdc); g_mdc = NULL; }
    g_mw = g_mh = 0;
}

HBRUSH ui_edit_brush(void) { return br_bg_edit; }
HFONT  ui_edit_font(void)  { return f_tab; }

static int text_w(HFONT f, const wchar_t *s)
{
    if (!g_mdc || !s) return 0;
    HGDIOBJ o = SelectObject(g_mdc, f);
    SIZE sz;
    sz.cx = sz.cy = 0;
    GetTextExtentPoint32W(g_mdc, s, (int)wcslen(s), &sz);
    SelectObject(g_mdc, o);
    return sz.cx;
}

static void text(HDC dc, const wchar_t *s, RECT r, HFONT f, COLORREF c, UINT fmt)
{
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
    SelectObject(dc, of);
}

/* 按字符折行（GDI 的 DT_WORDBREAK 对没有空格的中文不可靠） */
static void text_wrap(HDC dc, const wchar_t *s, RECT r, HFONT f, COLORREF c, int max_lines)
{
    if (!s || !s[0]) return;

    int maxw = r.right - r.left;
    int lineh = SZ(17);
    const wchar_t *p = s;
    int y = r.top;

    for (int line = 0; line < max_lines && *p; line++) {
        wchar_t tmp[128];
        int take = 0;

        while (p[take] && take < 127) {
            int n = take + 1;
            memcpy(tmp, p, (size_t)n * sizeof(wchar_t));
            tmp[n] = 0;
            if (text_w(f, tmp) > maxw) break;
            take = n;
        }
        if (take == 0) take = 1;

        memcpy(tmp, p, (size_t)take * sizeof(wchar_t));
        tmp[take] = 0;

        RECT lr;
        lr.left = r.left; lr.right = r.right;
        lr.top = y; lr.bottom = y + lineh;
        text(dc, tmp, lr, f, c, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);

        p += take;
        y += lineh;
    }
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

/* 用矩形填充画十字，保证横竖两笔严格对称、像素对齐（用 LineTo 会因笔宽取整而偏） */
static void plus_px(HDC dc, int cx, int cy, int arm, int thick, COLORREF c)
{
    int t = thick < 1 ? 1 : thick;
    int h0 = cy - t / 2;
    int v0 = cx - t / 2;

    /* 按实际像素比对微调：横线左移 1px、右端再延长 1px；竖线上移 1px、下端再延长 1px */
    RECT h = { cx - arm - 1, h0, cx + arm + 1, h0 + t };
    RECT v = { v0, cy - arm - 1, v0 + t, cy + arm + 1 };

    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &h, br);
    FillRect(dc, &v, br);
    DeleteObject(br);
}

/* ---------------- 顶部布局 ---------------- */
void ui_layout(int cw, UiLayout *L)
{
    if (!L) return;
    memset(L, 0, sizeof(*L));

    const int PAD = SZ(20);
    int top = SZ(14);
    int h   = SZ(30);

    L->row.left   = PAD;
    L->row.right  = cw - PAD;
    L->row.top    = top;
    L->row.bottom = top + h;

    int rb = SZ(28);
    L->refresh.right  = L->row.right;
    L->refresh.left   = L->refresh.right - rb;
    L->refresh.top    = top;
    L->refresh.bottom = top + h;

    int n = g_app.count;
    if (n < 0) n = 0;
    if (n > MAX_CITIES) n = MAX_CITIES;
    L->ntab = n;

    int tabmax = L->refresh.left - SZ(50) - PAD;   /* 右侧预留更新时间 */
    int addw = SZ(30);
    int gap  = SZ(5);
    int padx = SZ(8);
    int closw = (n > 1) ? SZ(13) : 0;

    int nat[MAX_CITIES];
    long sum = 0;
    for (int i = 0; i < n; i++) {
        wchar_t s[96];
        city_short(g_app.cities[i].full, s, 96);
        nat[i] = text_w(f_tab, s) + padx * 2 + closw;
        if (nat[i] < SZ(52)) nat[i] = SZ(52);
        sum += nat[i];
    }

    double k = 1.0;
    int total = (int)sum + gap * n + addw;
    if (total > tabmax && sum > 0) {
        int room = tabmax - gap * n - addw;
        if (room < n * SZ(44)) room = n * SZ(44);
        k = (double)room / (double)sum;
    }

    int x = PAD;
    for (int i = 0; i < n; i++) {
        int w = (int)lround(nat[i] * k);
        if (w < SZ(44)) w = SZ(44);

        L->tabs[i].left   = x;
        L->tabs[i].right  = x + w;
        L->tabs[i].top    = top;
        L->tabs[i].bottom = top + h;

        if (closw) {
            L->close[i].left   = L->tabs[i].right - closw - SZ(3);
            L->close[i].right  = L->tabs[i].right - SZ(1);
            L->close[i].top    = top;
            L->close[i].bottom = top + h;
        }
        x += w + gap;
    }

    L->add.left   = x;
    L->add.right  = x + addw;
    L->add.top    = top;
    L->add.bottom = top + h;
}

/* ---------------- 24 小时卡片几何（绘制与命中检测共用） ---------------- */

/* 卡片位置由上方各块的固定高度累加得出，改布局时这里要同步 */
static void chart_geom(int cw, RECT *card)
{
    const int PAD = SZ(20);
    int x = PAD;
    int w = cw - PAD * 2;

    int y = SZ(14) + SZ(30) + SZ(12);   /* 标签栏 */
    y += SZ(122);                       /* 主温度 */
    y += SZ(54) + SZ(12);               /* 主指标 */
    y += SZ(42) + SZ(12);               /* 次要数据 */

    card->left = x;
    card->right = x + w;
    card->top = y;
    card->bottom = y + SZ(126);
}

static void chart_tabs_geom(const RECT *card, RECT t[2])
{
    int th = SZ(18);
    int ty = card->top + SZ(7);

    int w2 = text_w(f_tiny, L"降水量") + SZ(16);
    int w1 = text_w(f_tiny, L"温度") + SZ(16);

    t[1].right = card->right - SZ(12);
    t[1].left  = t[1].right - w2;
    t[0].right = t[1].left - SZ(4);
    t[0].left  = t[0].right - w1;

    for (int i = 0; i < 2; i++) {
        t[i].top = ty;
        t[i].bottom = ty + th;
    }
}

int ui_chart_hit(int cw, POINT pt)
{
    RECT card, t[2];
    chart_geom(cw, &card);
    chart_tabs_geom(&card, t);

    for (int i = 0; i < 2; i++)
        if (PtInRect(&t[i], pt)) return i;
    return -1;
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

static double nice_step(double raw)
{
    if (raw <= 0) return 1;
    double e = 1;
    while (raw >= 10) { raw /= 10; e *= 10; }
    while (raw < 1)   { raw *= 10; e /= 10; }
    double s = raw <= 1 ? 1 : (raw <= 2 ? 2 : (raw <= 5 ? 5 : 10));
    return s * e;
}

static void hline(HDC dc, int x1, int x2, int y, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, x1, y, NULL);
    LineTo(dc, x2 + 1, y);
    SelectObject(dc, op);
    DeleteObject(pen);
}

/* mode: 0 = 温度曲线，1 = 逐小时降水量 */
static void draw_hourly(HDC dc, RECT r, int mode)
{
    const Weather *w = g_app.w;
    int n = w ? w->hcount : 0;
    if (n < 2) return;

    RECT plot;
    plot.left   = r.left + SZ(30);      /* 左侧 Y 轴标签列 */
    plot.right  = r.right;
    plot.top    = r.top;
    plot.bottom = r.bottom;
    if (plot.right - plot.left < SZ(40)) return;

    int step = (plot.right - plot.left) / (n - 1);
    if (step < 1) step = 1;

    /* Y 轴范围 */
    double lo = 0, hi = 1, stepv = 1;
    if (mode == 0) {
        double mn = w->htemp[0], mx = w->htemp[0];
        for (int i = 1; i < n; i++) {
            if (w->htemp[i] < mn) mn = w->htemp[i];
            if (w->htemp[i] > mx) mx = w->htemp[i];
        }
        if (mx - mn < 3.0) { double c = (mx + mn) * 0.5; mn = c - 1.5; mx = c + 1.5; }

        stepv = nice_step((mx - mn) / 2.0);
        lo = floor(mn / stepv) * stepv;
        hi = lo + stepv * 2;
        while (hi < mx) { lo += stepv; hi += stepv; }
    } else {
        double mx = 0;
        for (int i = 0; i < n; i++) if (w->hprecip[i] > mx) mx = w->hprecip[i];
        stepv = 0.5;
        while (stepv * 2 < mx) stepv *= 2;
        lo = 0;
        hi = stepv * 2;
    }

    int top = plot.top;
    int bot = plot.bottom;

    /* 温度曲线：先铺曲线下方淡色填充 */
    POINT pts[HOURS_MAX];
    if (mode == 0) {
        for (int i = 0; i < n; i++) {
            double k = (w->htemp[i] - lo) / (hi - lo);
            if (k < 0) k = 0;
            if (k > 1) k = 1;
            pts[i].x = plot.left + step * i;
            pts[i].y = bot - (LONG)lround(k * (bot - top));
        }

        POINT poly[HOURS_MAX + 2];
        for (int i = 0; i < n; i++) poly[i] = pts[i];
        poly[n].x = pts[n - 1].x; poly[n].y = bot;
        poly[n + 1].x = pts[0].x; poly[n + 1].y = bot;

        HBRUSH ab = CreateSolidBrush(RGB(0x17, 0x2B, 0x3E));
        HGDIOBJ ob = SelectObject(dc, ab);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, poly, n + 2);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(ab);
    }

    /* 横向网格线 + Y 轴标签（画在填充之上） */
    for (int i = 0; i < 3; i++) {
        double v = hi - stepv * i;
        int gy = bot - (int)lround((v - lo) / (hi - lo) * (bot - top));

        hline(dc, plot.left, plot.right, gy, RGB(0x26, 0x2F, 0x3A));

        wchar_t lb[16];
        if (mode == 0) swprintf(lb, 16, L"%.0f°", v);
        else           swprintf(lb, 16, L"%.1f", v);

        RECT lr;
        lr.left = r.left; lr.right = plot.left - SZ(5);
        lr.top = gy - SZ(7); lr.bottom = gy + SZ(7);
        text(dc, lb, lr, f_tiny, C_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (mode == 0) {
        HPEN pen = CreatePen(PS_SOLID, SZ(2), C_ACCENT);
        HGDIOBJ opn = SelectObject(dc, pen);
        HGDIOBJ obr = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Polyline(dc, pts, n);
        SelectObject(dc, opn);
        SelectObject(dc, obr);
        DeleteObject(pen);

        for (int i = 0; i < n; i += 3)
            fill_circle(dc, pts[i].x, pts[i].y, SZ(2), C_ACCENT);

        /* 当前点标记 */
        fill_circle(dc, pts[0].x, pts[0].y, SZ(4), C_TEXT);
        fill_circle(dc, pts[0].x, pts[0].y, SZ(2), C_ACCENT);
    } else {
        double mx = 0;
        for (int i = 0; i < n; i++) if (w->hprecip[i] > mx) mx = w->hprecip[i];

        int bw = step * 3 / 5;
        if (bw < SZ(3)) bw = SZ(3);

        for (int i = 0; i < n; i++) {
            if (w->hprecip[i] <= 0.001) continue;

            int hgt = (int)lround(w->hprecip[i] / (hi - lo) * (bot - top));
            if (hgt < SZ(2)) hgt = SZ(2);

            RECT pr;
            pr.left   = plot.left + step * i - bw / 2;
            pr.right  = pr.left + bw;
            pr.top    = bot - hgt;
            pr.bottom = bot;
            fill_rr(dc, pr, bw / 2, C_ACCENT);
        }

        if (mx < 0.05) {
            RECT nr;
            nr.left = plot.left; nr.right = plot.right;
            nr.top = (plot.top + plot.bottom) / 2 - SZ(8);
            nr.bottom = nr.top + SZ(16);
            text(dc, L"未来 24 小时无降水", nr, f_small, C_MUTED,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    /* 时间刻度 */
    for (int i = 0; i < n; i += 3) {
        wchar_t h[16];
        swprintf(h, 16, L"%d时", w->hhour[i]);

        RECT tr;
        tr.left   = plot.left + step * i - SZ(20);
        tr.right  = plot.left + step * i + SZ(20);
        tr.top    = r.bottom - SZ(1);
        tr.bottom = r.bottom + SZ(13);
        text(dc, h, tr, f_tiny, C_MUTED, DT_CENTER | DT_TOP | DT_SINGLELINE);
    }
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

    /* 卡片小标题 + 日出日落 */
    RECT hd;
    hd.left  = left;
    hd.right = card.right - SZ(14);
    hd.top   = card.top + SZ(11);
    hd.bottom = hd.top + SZ(15);
    text(dc, L"未来 7 天", hd, f_tiny, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (w->sunrise[0] && w->sunset[0]) {
        wchar_t sun[48];
        swprintf(sun, 48, L"日出 %s · 日落 %s", w->sunrise, w->sunset);
        text(dc, sun, hd, f_tiny, C_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    int ry0 = card.top + SZ(30);

    for (int i = 0; i < n; i++) {
        const DayForecast *d = &w->days[i];
        int ry = ry0 + row_h * i;

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

static void draw_stat(HDC dc, RECT r, const wchar_t *label, const wchar_t *value, COLORREF vcol)
{
    fill_rr(dc, r, SZ(12), C_CARD);

    RECT lr;
    lr.left = r.left + SZ(12); lr.right = r.right - SZ(8);
    lr.top = r.top + SZ(9); lr.bottom = r.top + SZ(24);
    text(dc, label, lr, f_tiny, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT vr;
    vr.left = r.left + SZ(12); vr.right = r.right - SZ(8);
    vr.top = r.top + SZ(25); vr.bottom = r.bottom - SZ(6);
    text(dc, value, vr, f_pill, vcol, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* 蒲福风级（中国大陆通用 0~17 级），输入 km/h */
static int wind_scale(double kmh)
{
    static const int LB[17] = {
        1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118, 134, 150, 167, 184, 202
    };
    int lv = 0;
    for (int i = 0; i < 17; i++)
        if (kmh >= LB[i]) lv = i + 1;
    return lv;
}

/* 次要数据小卡片：更矮、字号更小，视觉上明显次于上面那排 */
static void draw_mini(HDC dc, RECT r, const wchar_t *label, const wchar_t *value)
{
    fill_rr(dc, r, SZ(10), C_CARD);

    RECT lr;
    lr.left = r.left + SZ(10); lr.right = r.right - SZ(6);
    lr.top = r.top + SZ(5); lr.bottom = r.top + SZ(17);
    text(dc, label, lr, f_tiny, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT vr;
    vr.left = r.left + SZ(10); vr.right = r.right - SZ(6);
    vr.top = r.top + SZ(18); vr.bottom = r.bottom - SZ(4);
    text(dc, value, vr, f_body, C_TEXT,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

/* AQI 等级配色（接口没给 color 时兜底） */static COLORREF aqi_level_color(int level)
{
    switch (level) {
    case 1:  return RGB(0x4C, 0xD1, 0x37);   /* 优 */
    case 2:  return RGB(0xF2, 0xD0, 0x4B);   /* 良 */
    case 3:  return RGB(0xFF, 0x9E, 0x3D);   /* 轻度污染 */
    case 4:  return RGB(0xFF, 0x5B, 0x5B);   /* 中度污染 */
    case 5:  return RGB(0xC0, 0x6B, 0xE0);   /* 重度污染 */
    case 6:  return RGB(0x9B, 0x3B, 0x3B);   /* 严重污染 */
    default: return C_TEXT;
    }
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

static void draw_tabbar(HDC dc, const UiLayout *L, int cw)
{
    int dragging = (g_app.drag_idx >= 0);

    for (int i = 0; i < L->ntab; i++) {
        int act = (i == g_app.active);
        int hov = (g_app.hover_tab == i);
        int drg = (i == g_app.drag_idx);

        if (act)           fill_rr(dc, L->tabs[i], SZ(9), C_CARD2);
        else if (drg)      fill_rr(dc, L->tabs[i], SZ(9), C_LINE);
        else if (hov)      fill_rr(dc, L->tabs[i], SZ(9), C_CARD);

        wchar_t s[96];
        city_short(g_app.cities[i].full, s, 96);

        /* 右侧要预留 × 的位置；为保证文字居中，左侧预留同样宽度 */
        RECT tr = L->tabs[i];
        int ins = SZ(8) + (g_app.count > 1 ? SZ(13) / 2 : 0);
        tr.left  += ins;
        tr.right -= ins;
        text(dc, s, tr, f_tab, (act || hov || drg) ? C_TEXT : C_MUTED,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (!dragging && g_app.count > 1 && hov) {
            text(dc, L"×", L->close[i], f_tab,
                 (g_app.hover_close == i) ? C_ERR : C_MUTED,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    /* ＋ 添加城市：正圆按钮，加号用矩形块绘制保证横竖对齐 */
    int ad = L->add.right - L->add.left;
    int acx = (L->add.left + L->add.right) / 2;
    int acy = (L->add.top + L->add.bottom) / 2;
    fill_circle(dc, acx, acy, ad / 2, g_app.hover_add ? C_CARD2 : C_CARD);
    plus_px(dc, acx, acy, SZ(5), SZ(2), g_app.hover_add ? C_TEXT : C_MUTED);

    /* 更新时间 + 刷新按钮 */
    int x0 = L->ntab > 0 ? L->add.right : L->row.left;
    RECT tr;
    tr.left = x0 + SZ(4);
    tr.right = L->refresh.left - SZ(8);
    tr.top = L->row.top;
    tr.bottom = L->row.bottom;

    wchar_t info[32] = L"";
    if (g_app.busy)     wcscpy(info, L"更新中");
    else if (g_app.w)   wcsncpy(info, g_app.w->updated, 31);
    text(dc, info, tr, f_tiny, C_MUTED, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    if (!g_app.busy && g_app.hover_refresh)
        fill_rr(dc, L->refresh, (L->refresh.right - L->refresh.left) / 2, C_CARD2);

    COLORREF rc = g_app.busy ? RGB(0x4A, 0x55, 0x62)
                             : (g_app.hover_refresh ? C_TEXT : C_MUTED);
    text(dc, L"\u21BB", L->refresh, f_icon, rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    (void)cw;
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

    UiLayout L;
    ui_layout(cw, &L);
    draw_tabbar(dc, &L, cw);

    int y = L.row.bottom + SZ(12);
    const Weather *wd = g_app.w;

    if (!wd) {
        if (g_app.state == ST_ERROR)
            center_note(dc, cw, ch, g_app.error, L"按 F5 或点右上角刷新重试");
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

    /* 降水描述：放在大号温度右侧的留白里，长文本自动换行 */
    if (wd->minutely[0]) {
        SIZE tsz;
        tsz.cx = 0;
        HGDIOBJ of = SelectObject(dc, f_big);
        GetTextExtentPoint32W(dc, buf, (int)wcslen(buf), &tsz);
        SelectObject(dc, of);

        RECT mr;
        mr.left   = x + tsz.cx + SZ(16);
        mr.right  = x + w - iconw - SZ(6);
        mr.top    = y + SZ(14);
        mr.bottom = y + SZ(74);
        if (mr.right - mr.left > SZ(72))
            text_wrap(dc, wd->minutely, mr, f_small, C_MUTED, 3);
    }

    RECT rc;
    rc.left = x; rc.right = x + w - iconw;
    rc.top = y + SZ(72); rc.bottom = y + SZ(72) + SZ(22);
    text(dc, wd->desc[0] ? wd->desc : wmo_text(wd->code), rc, f_cond, C_TEXT,
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
        COLORREF vcol = C_TEXT;
        if (i == 0) {
            wcscpy(lbl, L"相对湿度");
            swprintf(val, 32, L"%d%%", wd->humidity);
        } else if (i == 1) {
            swprintf(lbl, 32, L"风速 · %s", wind_dir_text(wd->wind_dir));
            swprintf(val, 32, L"%d级 · %.0f km/h", wind_scale(wd->wind), wd->wind);
        } else if (wd->has_aqi) {
            wcscpy(lbl, L"空气质量");
            swprintf(val, 32, L"%s %d", wd->aqi_cat[0] ? wd->aqi_cat : L"—", wd->aqi);
            vcol = wd->aqi_color ? wd->aqi_color : aqi_level_color(wd->aqi_level);
        } else {
            /* 没有空气质量数据时退回显示气压，不留空位 */
            wcscpy(lbl, L"气压");
            swprintf(val, 32, L"%.0f hPa", wd->pressure);
        }
        draw_stat(dc, p, lbl, val, vcol);
    }
    y += SZ(54) + SZ(12);

    /* ---- 次要数据：气压 / 能见度 / 云量 ---- */
    int g2  = SZ(10);
    int pw2 = (w - g2 * 2) / 3;
    for (int i = 0; i < 3; i++) {
        RECT p;
        p.left  = x + i * (pw2 + g2);
        p.right = p.left + pw2;
        p.top   = y;
        p.bottom = y + SZ(42);

        wchar_t lbl[16], val[24];
        switch (i) {
        case 0:
            wcscpy(lbl, L"气压");
            swprintf(val, 24, L"%.0f hPa", wd->pressure);
            break;
        case 1:
            wcscpy(lbl, L"能见度");
            if (wd->vis > 0) swprintf(val, 24, L"%.0f km", wd->vis);
            else             wcscpy(val, L"—");
            break;
        case 2:
            wcscpy(lbl, L"云量");
            swprintf(val, 24, L"%.0f%%", wd->cloud);
            break;
        }
        draw_mini(dc, p, lbl, val);
    }
    y += SZ(42) + SZ(12);

    /* ---- 24 小时 ---- */
    RECT hcard, ctabs[2];
    chart_geom(cw, &hcard);
    chart_tabs_geom(&hcard, ctabs);

    fill_rr(dc, hcard, SZ(14), C_CARD);

    RECT htitle;
    htitle.left = hcard.left + SZ(12); htitle.right = ctabs[0].left - SZ(8);
    htitle.top = hcard.top + SZ(7); htitle.bottom = hcard.top + SZ(23);
    text(dc, L"未来 24 小时", htitle, f_tiny, C_MUTED,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    /* 图表标签：温度 / 降水量 */
    for (int i = 0; i < 2; i++) {
        int act = (g_app.chart_tab == i);
        int hov = (g_app.hover_chart == i);

        if (act)       fill_rr(dc, ctabs[i], SZ(9), C_CARD2);
        else if (hov)  fill_rr(dc, ctabs[i], SZ(9), C_CARD);

        text(dc, i == 0 ? L"温度" : L"降水量", ctabs[i], f_tiny,
             (act || hov) ? C_TEXT : C_MUTED,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (wd->hcount > 1) {
        RECT hplot;
        hplot.left = hcard.left + SZ(14);
        hplot.right = hcard.right - SZ(14);
        hplot.top = hcard.top + SZ(30);
        hplot.bottom = hcard.bottom - SZ(18);
        draw_hourly(dc, hplot, g_app.chart_tab);
    }
    y = hcard.bottom + SZ(12);

    /* ---- 7 天 ---- */
    if (wd->dcount > 0) {
        RECT dcard;
        dcard.left = x; dcard.right = x + w;
        dcard.top = y;
        dcard.bottom = y + SZ(30) + SZ(26) * wd->dcount + SZ(10);
        fill_rr(dc, dcard, SZ(14), C_CARD);
        draw_daily(dc, dcard);
    }

    /* ---- 底栏：出错时在此提示，否则显示数据来源 ---- */
    RECT ft;
    ft.left = x; ft.right = x + w;
    ft.top = ch - SZ(26); ft.bottom = ch - SZ(12);
    if (g_app.state == ST_ERROR && g_app.error[0]) {
        text(dc, g_app.error, ft, f_tiny, C_ERR,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    } else {
        text(dc, qweather_ready() ? L"数据来源 和风天气" : L"数据来源 Open-Meteo",
             ft, f_tiny, C_MUTED, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    BitBlt(target, 0, 0, cw, ch, dc, 0, 0, SRCCOPY);
}
