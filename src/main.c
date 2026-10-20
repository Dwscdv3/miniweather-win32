/* 窗口、消息循环、后台拉取调度 */

#include "app.h"
#include "ui.h"

#include <windowsx.h>
#include <stdio.h>

#define MZ(x) MulDiv((x), g_dpi, 96)

AppState g_app;
int      g_dpi = 96;

static HWND    g_hwnd;
static HWND    g_edit;
static WNDPROC g_edit_old;
static wchar_t g_city[96] = L"北京";
static double  g_lat = 39.9042;
static double  g_lon = 116.4074;

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

static void start_job(Job *job)
{
    if (!job) return;
    if (g_app.busy) { free(job); return; }

    g_app.busy  = 1;
    g_app.state = ST_LOADING;
    g_app.error[0] = 0;
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

static void refresh(void)
{
    if (g_app.busy) return;

    Job *job = (Job *)calloc(1, sizeof(Job));
    if (!job) return;
    job->mode = 0;
    job->lat  = g_lat;
    job->lon  = g_lon;
    wcsncpy(job->city, g_city, 95);
    start_job(job);
}

/* ---------------- 城市输入框 ---------------- */

static void hide_city_edit(void)
{
    if (g_edit) ShowWindow(g_edit, SW_HIDE);
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && wp == VK_RETURN) { SendMessageW(g_hwnd, WM_APP + 3, 0, 0); return 0; }
    if (m == WM_KEYDOWN && wp == VK_ESCAPE) {
        hide_city_edit();
        SetFocus(g_hwnd);
        return 0;
    }
    if (m == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_edit_old, h, m, wp, lp);
}

static void ensure_edit(void)
{
    if (g_edit) return;

    RECT cr = ui_city_rect(MZ(WIN_W));
    g_edit = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | ES_AUTOHSCROLL,
                             cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top,
                             g_hwnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);
    if (!g_edit) return;

    g_edit_old = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)ui_edit_font(), TRUE);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(MZ(6), MZ(6)));
    SendMessageW(g_edit, EM_SETLIMITTEXT, 48, 0);
}

static void show_city_edit(void)
{
    ensure_edit();
    if (!g_edit) return;

    RECT cr = ui_city_rect(MZ(WIN_W));
    MoveWindow(g_edit, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top, TRUE);

    if (g_app.w) {
        wchar_t tmp[96];
        wcsncpy(tmp, g_app.w->city, 95);
        wchar_t *dot = wcsstr(tmp, L" · ");
        if (dot) *dot = 0;
        SendMessageW(g_edit, WM_SETTEXT, 0, (LPARAM)tmp);
    } else {
        SendMessageW(g_edit, WM_SETTEXT, 0, (LPARAM)L"");
    }

    ShowWindow(g_edit, SW_SHOW);
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
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

    Job *job = (Job *)calloc(1, sizeof(Job));
    if (!job) return;
    job->mode = 1;
    wcsncpy(job->query, buf, 95);
    start_job(job);
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

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        RECT *pr = (RECT *)lp;
        SetWindowPos(h, NULL, pr->left, pr->top,
                     pr->right - pr->left, pr->bottom - pr->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ui_fonts_recreate();
        if (g_edit) {
            SendMessageW(g_edit, WM_SETFONT, (WPARAM)ui_edit_font(), TRUE);
            RECT cr = ui_city_rect(MZ(WIN_W));
            MoveWindow(g_edit, cr.left, cr.top, cr.right - cr.left, cr.bottom - cr.top, TRUE);
        }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) refresh();
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_F5) { refresh(); return 0; }
        if (wp == 'C')   { show_city_edit(); return 0; }
        break;

    case WM_LBUTTONUP: {
        POINT pt;
        pt.x = GET_X_LPARAM(lp);
        pt.y = GET_Y_LPARAM(lp);
        RECT rc;
        GetClientRect(h, &rc);
        RECT cr = ui_city_rect(rc.right);
        if (PtInRect(&cr, pt)) {
            if (g_edit && IsWindowVisible(g_edit)) { hide_city_edit(); SetFocus(h); }
            else show_city_edit();
        }
        return 0;
    }

    case WM_SETCURSOR:
        return DefWindowProcW(h, m, wp, lp);

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_CARD);
        return (LRESULT)ui_edit_brush();
    }

    case WM_APP + 3:      /* 输入框回车 */
        commit_city();
        return 0;

    case WM_APP_WEATHER_OK: {
        Weather *w = (Weather *)lp;
        if (g_app.w) weather_free(g_app.w);
        g_app.w     = w;
        g_app.state = ST_READY;
        g_app.busy  = 0;
        g_app.error[0] = 0;

        wcsncpy(g_city, w->city, 95);
        wcsncpy(g_app.city, w->city, 95);
        g_lat = w->lat;
        g_lon = w->lon;
        config_save(w->city, w->lat, w->lon);

        InvalidateRect(h, NULL, FALSE);
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

    ui_init();
    config_load(g_city, 96, &g_lat, &g_lon);
    wcsncpy(g_app.city, g_city, 95);

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

    SetTimer(g_hwnd, 1, 10 * 60 * 1000, NULL);
    refresh();

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
