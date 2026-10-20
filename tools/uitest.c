/* 开发辅助：对运行中的窗口做交互冒烟测试（换城市 / 刷新）。
 * 用法：uitest.exe <城市名> */

#include <windows.h>
#include <stdio.h>

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *city = argc > 1 ? argv[1] : L"上海";

    HWND h = FindWindowW(L"MiniWeatherWnd", NULL);
    if (!h) {
        printf("window not found\n");
        return 1;
    }
    printf("window ok\n");

    /* 按 C 打开城市输入框 */
    PostMessageW(h, WM_KEYDOWN, 'C', 0);
    Sleep(400);

    HWND e = FindWindowExW(h, NULL, L"EDIT", NULL);
    if (!e || !IsWindowVisible(e)) {
        printf("FAIL: city edit did not appear\n");
        return 1;
    }
    printf("edit visible ok\n");

    SendMessageW(e, WM_SETTEXT, 0, (LPARAM)city);
    PostMessageW(e, WM_KEYDOWN, VK_RETURN, 0);
    printf("submitted %ls\n", city);
    Sleep(3500);

    if (IsWindowVisible(e)) {
        printf("FAIL: edit still visible after enter\n");
        return 1;
    }
    printf("edit hidden ok\n");

    /* F5 刷新 */
    PostMessageW(h, WM_KEYDOWN, VK_F5, 0);
    Sleep(3500);
    printf("refresh triggered\n");

    printf("PASS\n");
    return 0;
}
