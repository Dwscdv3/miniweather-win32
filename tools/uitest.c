/* 开发辅助：对运行中的窗口做交互冒烟测试。
 *   uitest.exe add <城市>     按 C 打开输入框、输入城市、回车
 *   uitest.exe refresh        F5 刷新
 *   uitest.exe hover <x> <y>  客户端坐标上的鼠标悬停
 *   uitest.exe click <x> <y>  客户端坐标上的左键点击
 * 坐标按 96 DPI 的逻辑像素换算，便于对照布局代码。 */

#include <windows.h>
#include <stdio.h>

static HWND g_h;

static int open_edit(HWND h)
{
    PostMessageW(h, WM_KEYDOWN, 'C', 0);
    Sleep(400);
    HWND e = FindWindowExW(h, NULL, L"EDIT", NULL);
    if (!e || !IsWindowVisible(e)) {
        printf("FAIL: city edit did not appear\n");
        return 0;
    }
    return 1;
}

static int cmd_add(HWND h, const wchar_t *city)
{
    if (!open_edit(h)) return 1;

    HWND e = FindWindowExW(h, NULL, L"EDIT", NULL);
    SendMessageW(e, WM_SETTEXT, 0, (LPARAM)city);
    PostMessageW(e, WM_KEYDOWN, VK_RETURN, 0);
    Sleep(4000);

    if (IsWindowVisible(e)) {
        printf("FAIL: edit still visible after enter\n");
        return 1;
    }
    printf("OK: submitted city\n");
    return 0;
}

static int cmd_click(HWND h, int x, int y)
{
    PostMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    Sleep(1200);
    printf("OK: click %d,%d\n", x, y);
    return 0;
}

static int cmd_hover(HWND h, int x, int y)
{
    PostMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
    Sleep(300);
    printf("OK: hover %d,%d\n", x, y);
    return 0;
}

/* 真正把系统光标移到窗口内某点，用于验证悬停样式 */
static int cmd_hover_real(HWND h, int x, int y)
{
    POINT pt;
    pt.x = x;
    pt.y = y;
    ClientToScreen(h, &pt);
    SetCursorPos(pt.x, pt.y);
    Sleep(500);
    printf("OK: real hover %d,%d\n", x, y);
    return 0;
}

int wmain(int argc, wchar_t **argv)
{
    g_h = FindWindowW(L"MiniWeatherWnd", NULL);
    if (!g_h) {
        printf("window not found\n");
        return 1;
    }

    const wchar_t *cmd = argc > 1 ? argv[1] : L"";

    if (!wcscmp(cmd, L"add") && argc > 2)
        return cmd_add(g_h, argv[2]);

    if (!wcscmp(cmd, L"refresh")) {
        PostMessageW(g_h, WM_KEYDOWN, VK_F5, 0);
        Sleep(3500);
        printf("OK: refresh\n");
        return 0;
    }

    if (!wcscmp(cmd, L"hover") && argc > 3)
        return cmd_hover(g_h, _wtoi(argv[2]), _wtoi(argv[3]));

    if (!wcscmp(cmd, L"hoverreal") && argc > 3)
        return cmd_hover_real(g_h, _wtoi(argv[2]), _wtoi(argv[3]));

    if (!wcscmp(cmd, L"click") && argc > 3)
        return cmd_click(g_h, _wtoi(argv[2]), _wtoi(argv[3]));

    printf("usage: uitest add <city> | refresh | hover <x> <y> | hoverreal <x> <y> | click <x> <y>\n");
    return 2;
}
