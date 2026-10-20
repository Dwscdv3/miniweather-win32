/* 窗口、消息循环、常用城市管理与后台拉取调度 */

#include "app.h"
#include "ui.h"

#include <windowsx.h>
#include <shellapi.h>
#include <math.h>
#include <stdio.h>

#define MZ(x) MulDiv((x), g_dpi, 96)

AppState g_app;
ApiConfig g_api;
int      g_dpi = 96;

static HWND    g_hwnd;
static HWND    g_edit;
static WNDPROC g_edit_old;
static int     g_job_mode;          /* 当前后台任务的类型 */
static wchar_t g_pending_query[96]; /* 忙时提交的城市，等当前任务结束后再查 */

/* 鼠标按下状态：用来区分“点击”和“拖拽排序” */
static struct {
    int   hit;
    int   idx;
    POINT pt;
    int   dragging;
} g_press;

/* ---------------- 常用城市 ---------------- */

/* --host / --key 写入注册表后立即生效；带 --reset-api 则清除并回退 Open-Meteo */
static void api_args(void)
{
    wchar_t host[128] = L"";
    wchar_t key[64]   = L"";
    int changed = 0;

    api_load(host, 128, key, 64);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; i++) {
            if (!wcscmp(argv[i], L"--host") && i + 1 < argc) {
                wcsncpy(host, argv[++i], 127);
                host[127] = 0;
                changed = 1;
            } else if (!wcscmp(argv[i], L"--key") && i + 1 < argc) {
                wcsncpy(key, argv[++i], 63);
                key[63] = 0;
                changed = 1;
            } else if (!wcscmp(argv[i], L"--reset-api")) {
                host[0] = 0;
                key[0]  = 0;
                changed = 1;
            }
        }
        LocalFree(argv);
    }

    if (changed) {
        trim_w(host);
        trim_w(key);
        api_save(host, key);
    }
    api_load(g_api.host, 128, g_api.key, 64);
}

static void save_store(void)
{
    if (g_app.count > 0)
        store_save(g_app.cities, g_app.count, g_app.cities[g_app.active].full);
}

static void cities_init(void)
{
    wchar_t act[96] = L"";

    if (store_load(g_app.cities, MAX_CITIES, &g_app.count, act, 96)) {
        g_app.active = 0;
        for (int i = 0; i < g_app.count; i++) {
            if (!wcscmp(g_app.cities[i].full, act)) { g_app.active = i; break; }
        }
        return;
    }

    wchar_t city[96] = L"";
    double lat = 0, lon = 0;
    if (legacy_load(city, 96, &lat, &lon)) {
        wcsncpy(g_app.cities[0].full, city, 95);
        g_app.cities[0].lat = lat;
        g_app.cities[0].lon = lon;
    } else {
        wcscpy(g_app.cities[0].full, L"北京");
        g_app.cities[0].lat = 39.9042;
        g_app.cities[0].lon = 116.4074;
    }
    g_app.count  = 1;
    g_app.active = 0;
    save_store();
}

static int find_city(const wchar_t *name, double lat, double lon)
{
    for (int i = 0; i < g_app.count; i++) {
        if (name && name[0] && !wcscmp(g_app.cities[i].full, name)) return i;
        if (fabs(g_app.cities[i].lat - lat) < 0.05 && fabs(g_app.cities[i].lon - lon) < 0.05) return i;
    }
    return -1;
}

/* ---------------- 后台任务 ---------------- */

static DWORD WINAPI worker(LPVOID param)
{
    Job *job = (Job *)param;
    wchar_t err[192] = L"";

    Weather *w = weather_fetch(job, err, 192);
    if (w) {
        if (!PostMessageW(g_hwnd, WM_APP_WEATHER_OK, 0, (LPARAM)w)) weather_free(w);
    } else {
        wchar_t *e = (wchar_t *)calloc(192, sizeof(wchar_t));
        if (e) {
            wcsncpy(e, err[0] ? err : L"获取天气失败", 191);
            if (!PostMessageW(g_hwnd, WM_APP_WEATHER_ERR, 0, (LPARAM)e)) free(e);
        }
    }
    free(job);
    return 0;
}

static void start_job(Job *job, int mode)
{
    if (!job) return;
    if (g_app.busy) { free(job); return; }

    g_app.busy     = 1;
    g_app.state    = ST_LOADING;
    g_app.error[0] = 0;
    g_job_mode     = mode;
    InvalidateRect(g_hwnd, NULL, FALSE);

    HANDLE th = CreateThread(NULL, 0, worker, job, 0, NULL);
    if (th) {
        CloseHandle(th);
    } else {
        free(job);
        g_app.busy  = 0;
        g_app.state = ST_ERROR;
        wcscpy(g_app.error, L"无法创建后台线程");
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

static void refresh_active(void)
{
    if (g_app.busy || g_app.count <= 0) return;

    const City *c = &g_app.cities[g_app.active];
    Job *job = (Job *)calloc(1, sizeof(Job));
    if (!job) return;

    job->mode = 0;
    job->lat  = c->lat;
    job->lon  = c->lon;
    wcsncpy(job->city, c->full, 95);
    start_job(job, 0);
}

/* 忙时提交的搜索请求排队执行（只保留最后一条） */
static void run_pending_query(void)
{
    if (!g_pending_query[0] || g_app.busy) return;

    wchar_t q[96];
    wcsncpy(q, g_pending_query, 95);
    q[95] = 0;
    g_pending_query[0] = 0;

    Job *job = (Job *)calloc(1, sizeof(Job));
    if (!job) return;
    job->mode = 1;
    wcsncpy(job->query, q, 95);
    start_job(job, 1);
}

static void activate_city(int idx)
{
    if (idx < 0 || idx >= g_app.count) return;
    if (idx == g_app.active && g_app.state != ST_ERROR) return;

    g_app.active   = idx;
    g_app.state    = ST_LOADING;
    g_app.error[0] = 0;
    save_store();
    InvalidateRect(g_hwnd, NULL, FALSE);

    refresh_active();   /* 若正忙，旧结果会在返回时因坐标不符被丢弃并重取 */
}

static void delete_city(int idx)
{
    if (g_app.count <= 1 || idx < 0 || idx >= g_app.count) return;

    int was_active = (idx == g_app.active);

    memmove(&g_app.cities[idx], &g_app.cities[idx + 1],
            (size_t)(g_app.count - idx - 1) * sizeof(City));
    g_app.count--;

    if (g_app.active > idx) g_app.active--;
    else if (g_app.active >= g_app.count) g_app.active = g_app.count - 1;

    save_store();
    g_app.hover_tab = -1;
    g_app.hover_close = -1;
    InvalidateRect(g_hwnd, NULL, FALSE);

    if (was_active) {
        g_app.state = ST_LOADING;
        refresh_active();
    }
}

/* 拖拽排序：把 from 位置的标签移到 to 位置，当前选中项跟着走 */
static void reorder_city(int from, int to)
{
    if (from == to || from < 0 || to < 0) return;
    if (from >= g_app.count || to >= g_app.count) return;

    City tmp = g_app.cities[from];

    if (from < to) {
        memmove(&g_app.cities[from], &g_app.cities[from + 1],
                (size_t)(to - from) * sizeof(City));
        if (g_app.active == from)            g_app.active = to;
        else if (g_app.active > from && g_app.active <= to) g_app.active--;
    } else {
        memmove(&g_app.cities[to + 1], &g_app.cities[to],
                (size_t)(from - to) * sizeof(City));
        if (g_app.active == from)            g_app.active = to;
        else if (g_app.active >= to && g_app.active < from) g_app.active++;
    }
    g_app.cities[to] = tmp;

    save_store();
}

/* 拖拽时按最近的标签中心决定落点 */
static int tab_at_x(int x)
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    UiLayout L;
    ui_layout(rc.right, &L);
    if (L.ntab <= 0) return -1;

    int best = 0;
    int bestd = -1;
    for (int i = 0; i < L.ntab; i++) {
        int cx = (L.tabs[i].left + L.tabs[i].right) / 2;
        int d = cx > x ? cx - x : x - cx;
        if (bestd < 0 || d < bestd) { bestd = d; best = i; }
    }
    return best;
}

/* ---------------- 命中测试 ---------------- */

enum { HIT_NONE = 0, HIT_TAB, HIT_CLOSE, HIT_ADD, HIT_REFRESH, HIT_CHART };

static int hit_test(POINT pt, int *idx)
{
    if (idx) *idx = -1;

    RECT rc;
    GetClientRect(g_hwnd, &rc);
    UiLayout L;
    ui_layout(rc.right, &L);

    for (int i = 0; i < L.ntab; i++) {
        if (g_app.count > 1 && PtInRect(&L.close[i], pt)) {
            if (idx) *idx = i;
            return HIT_CLOSE;
        }
        if (PtInRect(&L.tabs[i], pt)) {
            if (idx) *idx = i;
            return HIT_TAB;
        }
    }
    if (PtInRect(&L.add, pt)) return HIT_ADD;
    if (!g_app.busy && PtInRect(&L.refresh, pt)) return HIT_REFRESH;

    int ci = ui_chart_hit(rc.right, pt);
    if (ci >= 0) {
        if (idx) *idx = ci;
        return HIT_CHART;
    }
    return HIT_NONE;
}

static void set_hover(POINT pt)
{
    int idx = -1;
    int hit = hit_test(pt, &idx);

    int ht = (hit == HIT_TAB || hit == HIT_CLOSE) ? idx : -1;
    int hc = (hit == HIT_CLOSE) ? idx : -1;
    int ha = (hit == HIT_ADD) ? 1 : 0;
    int hr = (hit == HIT_REFRESH) ? 1 : 0;
    int hg = (hit == HIT_CHART) ? idx : -1;

    if (ht != g_app.hover_tab || hc != g_app.hover_close ||
        ha != g_app.hover_add || hr != g_app.hover_refresh ||
        hg != g_app.hover_chart) {
        g_app.hover_tab     = ht;
        g_app.hover_close   = hc;
        g_app.hover_add     = ha;
        g_app.hover_refresh = hr;
        g_app.hover_chart   = hg;
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

static void clear_hover(void)
{
    if (g_app.hover_tab < 0 && g_app.hover_close < 0 &&
        !g_app.hover_add && !g_app.hover_refresh && g_app.hover_chart < 0) return;

    g_app.hover_tab = g_app.hover_close = -1;
    g_app.hover_chart = -1;
    g_app.hover_add = g_app.hover_refresh = 0;
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ---------------- 城市输入框 ---------------- */

static void hide_city_edit(void)
{
    if (g_edit && IsWindowVisible(g_edit)) ShowWindow(g_edit, SW_HIDE);
}

static void edit_rect(RECT *out)
{
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    UiLayout L;
    ui_layout(rc.right, &L);

    out->left   = L.row.left - MZ(2);
    out->right  = L.refresh.left - MZ(6);
    out->top    = L.row.top - MZ(3);
    out->bottom = L.row.bottom + MZ(3);
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(g_hwnd, WM_APP_COMMIT_CITY, 0, 0);
        return 0;
    }
    if (m == WM_KEYDOWN && wp == VK_ESCAPE) {
        hide_city_edit();
        SetFocus(g_hwnd);
        return 0;
    }
    if (m == WM_KILLFOCUS) {
        hide_city_edit();
        return 0;
    }
    if (m == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_edit_old, h, m, wp, lp);
}

static void ensure_edit(void)
{
    if (g_edit) return;

    RECT r;
    edit_rect(&r);

    g_edit = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | ES_AUTOHSCROLL,
                             r.left, r.top, r.right - r.left, r.bottom - r.top,
                             g_hwnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    if (!g_edit) return;

    g_edit_old = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)ui_edit_font(), TRUE);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(MZ(8), MZ(8)));
    SendMessageW(g_edit, EM_SETLIMITTEXT, 48, 0);
}

static void show_city_edit(void)
{
    ensure_edit();
    if (!g_edit) return;

    RECT r;
    edit_rect(&r);
    MoveWindow(g_edit, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);

    SendMessageW(g_edit, WM_SETTEXT, 0, (LPARAM)L"");
    ShowWindow(g_edit, SW_SHOW);
    SetFocus(g_edit);
}

static void commit_city(void)
{
    if (!g_edit) return;

    wchar_t buf[96] = L"";
    GetWindowTextW(g_edit, buf, 96);
    trim_w(buf);
    hide_city_edit();
    SetFocus(g_hwnd);
    if (!buf[0]) return;

    /* 已经是常用城市就直接切换，避免重复添加 */
    for (int i = 0; i < g_app.count; i++) {
        wchar_t sn[96];
        city_short(g_app.cities[i].full, sn, 96);
        if (!wcscmp(sn, buf) || !wcscmp(g_app.cities[i].full, buf)) {
            activate_city(i);
            return;
        }
    }

    if (g_app.busy) {
        /* 正在拉取：排队，等这次结束后自动查 */
        wcsncpy(g_pending_query, buf, 95);
        g_pending_query[95] = 0;
        return;
    }

    Job *job = (Job *)calloc(1, sizeof(Job));
    if (!job) return;
    job->mode = 1;
    wcsncpy(job->query, buf, 95);
    start_job(job, 1);
}

/* ---------------- 窗口过程 ---------------- */

static void use_dark_titlebar(HWND h)
{
    HMODULE d = LoadLibraryW(L"dwmapi.dll");
    if (!d) return;
    typedef HRESULT (WINAPI *PFN_DWM)(HWND, DWORD, LPCVOID, DWORD);
    PFN_DWM fn = (PFN_DWM)(void *)GetProcAddress(d, "DwmSetWindowAttribute");
    if (fn) {
        BOOL yes = TRUE;
        if (FAILED(fn(h, 20, &yes, sizeof(yes)))) fn(h, 19, &yes, sizeof(yes));
    }
    FreeLibrary(d);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        ui_paint(dc, rc.right, rc.bottom);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_SIZE:
        InvalidateRect(h, NULL, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);

        int idx = -1;
        g_press.hit      = hit_test(pt, &idx);
        g_press.idx      = idx;
        g_press.pt       = pt;
        g_press.dragging = 0;

        if (g_press.hit == HIT_TAB) SetCapture(h);   /* 有拖拽可能 */
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);

        if (GetCapture() == h && g_press.hit == HIT_TAB && g_app.count > 1) {
            if (!g_press.dragging) {
                int dx = pt.x - g_press.pt.x;
                int dy = pt.y - g_press.pt.y;
                if (dx > MZ(4) || dx < -MZ(4) || dy > MZ(10) || dy < -MZ(10)) {
                    g_press.dragging = 1;
                    g_app.drag_idx   = g_press.idx;
                    InvalidateRect(h, NULL, FALSE);
                }
            }
            if (g_press.dragging) {
                int target = tab_at_x(pt.x);
                if (target >= 0 && target != g_app.drag_idx) {
                    reorder_city(g_app.drag_idx, target);
                    g_app.drag_idx = target;
                    InvalidateRect(h, NULL, FALSE);
                }
                return 0;
            }
        }

        set_hover(pt);

        TRACKMOUSEEVENT tme;
        tme.cbSize      = sizeof(tme);
        tme.dwFlags     = TME_LEAVE;
        tme.hwndTrack   = h;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_CAPTURECHANGED:
        if (g_press.dragging) {
            g_press.dragging = 0;
            g_app.drag_idx   = -1;
            InvalidateRect(h, NULL, FALSE);
        }
        g_press.hit = HIT_NONE;
        return 0;

    case WM_MOUSELEAVE:
        clear_hover();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            if (hit_test(pt, NULL) != HIT_NONE) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONUP: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);

        if (g_press.dragging) {                 /* 拖拽结束，吞掉这次点击 */
            g_press.dragging = 0;
            g_app.drag_idx   = -1;
            if (GetCapture() == h) ReleaseCapture();
            InvalidateRect(h, NULL, FALSE);
            return 0;
        }
        if (GetCapture() == h) ReleaseCapture();
        g_press.hit = HIT_NONE;

        int idx = -1;
        switch (hit_test(pt, &idx)) {
        case HIT_CLOSE:   delete_city(idx); break;
        case HIT_TAB:     activate_city(idx); break;
        case HIT_ADD:     show_city_edit(); break;
        case HIT_REFRESH: refresh_active(); break;
        case HIT_CHART:   g_app.chart_tab = (idx == 1) ? 1 : 0;
                          InvalidateRect(h, NULL, FALSE);
                          break;
        default: break;
        }
        return 0;
    }

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        RECT *pr = (RECT *)lp;
        SetWindowPos(h, NULL, pr->left, pr->top,
                     pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ui_fonts_recreate();
        if (g_edit) {
            SendMessageW(g_edit, WM_SETFONT, (WPARAM)ui_edit_font(), TRUE);
            RECT r;
            edit_rect(&r);
            MoveWindow(g_edit, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) refresh_active();
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_F5) { refresh_active(); return 0; }
        if (wp == 'C')   { show_city_edit(); return 0; }
        break;

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_CARD);
        return (LRESULT)ui_edit_brush();
    }

    case WM_APP_COMMIT_CITY:
        commit_city();
        return 0;

    case WM_APP_WEATHER_OK: {
        Weather *w = (Weather *)lp;

        /* 搜索回来的新城市：入列或复用已有条目 */
        if (g_job_mode == 1) {
            int idx = find_city(w->city, w->lat, w->lon);
            if (idx < 0) {
                if (g_app.count >= MAX_CITIES) {
                    weather_free(w);
                    g_app.busy  = 0;
                    g_app.state = ST_ERROR;
                    g_app.error[0] = 0;
                    swprintf(g_app.error, 192, L"最多保存 %d 个常用城市", MAX_CITIES);
                    InvalidateRect(h, NULL, FALSE);
                    run_pending_query();
                    return 0;
                }
                idx = g_app.count++;
                wcsncpy(g_app.cities[idx].full, w->city, 95);
                g_app.cities[idx].lat = w->lat;
                g_app.cities[idx].lon = w->lon;
            }
            g_app.active = idx;
            save_store();
        }

        /* 结果必须对应当前选中的城市，否则丢弃并重新拉取 */
        const City *cur = &g_app.cities[g_app.active];
        if (fabs(w->lat - cur->lat) > 0.02 || fabs(w->lon - cur->lon) > 0.02) {
            weather_free(w);
            g_app.busy = 0;
            if (g_pending_query[0]) run_pending_query();
            else                    refresh_active();
            return 0;
        }

        if (g_app.w) weather_free(g_app.w);
        g_app.w        = w;
        g_app.state    = ST_READY;
        g_app.busy     = 0;
        g_app.error[0] = 0;

        InvalidateRect(h, NULL, FALSE);
        run_pending_query();
        return 0;
    }

    case WM_APP_WEATHER_ERR: {
        wchar_t *e = (wchar_t *)lp;
        if (e) {
            wcsncpy(g_app.error, e, 191);
            g_app.error[191] = 0;
            free(e);
        } else {
            wcscpy(g_app.error, L"获取天气失败");
        }
        g_app.state = ST_ERROR;
        g_app.busy  = 0;
        InvalidateRect(h, NULL, FALSE);
        run_pending_query();
        return 0;
    }

    case WM_DESTROY:
        KillTimer(h, 1);
        if (g_app.w) { weather_free(g_app.w); g_app.w = NULL; }
        ui_free();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

/* ---------------- 入口 ---------------- */

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, LPWSTR cmdline, int show)
{
    (void)prev;
    (void)cmdline;

    HMODULE u32 = GetModuleHandleW(L"user32.dll");

    typedef BOOL (WINAPI *PFN_CTX)(HANDLE);
    PFN_CTX pctx = (PFN_CTX)(void *)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (pctx) pctx((HANDLE)-4);
    else SetProcessDPIAware();

    typedef UINT (WINAPI *PFN_DPISYS)(void);
    PFN_DPISYS pdpi = (PFN_DPISYS)(void *)GetProcAddress(u32, "GetDpiForSystem");
    if (pdpi) {
        g_dpi = (int)pdpi();
    } else {
        HDC sdc = GetDC(NULL);
        g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
        ReleaseDC(NULL, sdc);
    }
    if (g_dpi <= 0) g_dpi = 96;

    api_args();
    cities_init();
    g_app.hover_tab   = -1;      /* 0 是合法标签下标，必须显式初始化 */
    g_app.hover_close = -1;
    g_app.hover_chart = -1;
    g_app.drag_idx    = -1;
    g_app.chart_tab   = 0;
    ui_init();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(C_BG);
    wc.lpszClassName = L"MiniWeatherWnd";
    if (!RegisterClassExW(&wc)) return 1;

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc;
    rc.left = 0; rc.top = 0;
    rc.right = MZ(WIN_W);
    rc.bottom = MZ(WIN_H);
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    int ww = rc.right - rc.left;
    int wh = rc.bottom - rc.top;

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2;
    int wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;

    g_hwnd = CreateWindowExW(0, L"MiniWeatherWnd", APP_TITLE, style | WS_CLIPCHILDREN,
                             wx, wy, ww, wh, NULL, NULL, hi, NULL);
    if (!g_hwnd) return 1;

    use_dark_titlebar(g_hwnd);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    /* 和风一轮刷新要 4 个请求（now/24h/7d/air），免费额度 1000 次/天；
       15 分钟一轮 ≈ 384 次/天，给切城市留出余量 */
    UINT interval = qweather_ready() ? 15u * 60u * 1000u : 10u * 60u * 1000u;
    SetTimer(g_hwnd, 1, interval, NULL);
    refresh_active();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
