/* 开发辅助：把运行中的主窗口截成原始 BGRA 位图，供文档截图使用。
 * 用法：shot.exe [输出路径]  （默认 build\shot.raw -> 由 tools/raw2png.py 转 PNG） */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int wmain(int argc, wchar_t **argv)
{
    const wchar_t *out = argc > 1 ? argv[1] : L"build\\shot.raw";
    BOOL whole_screen = (argc > 2 && !wcscmp(argv[2], L"--screen"));
    BOOL front        = (argc > 2 && !wcscmp(argv[2], L"--front"));

    HDC sdc = GetDC(NULL);
    HDC mdc = NULL;
    HBITMAP bmp = NULL;
    HGDIOBJ old = NULL;
    int w = 0, ht = 0;

    if (whole_screen) {
        w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        ht = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (w <= 0 || ht <= 0) return 1;
        mdc = CreateCompatibleDC(sdc);
        bmp = CreateCompatibleBitmap(sdc, w, ht);
        old = SelectObject(mdc, bmp);
        BitBlt(mdc, 0, 0, w, ht, sdc, GetSystemMetrics(SM_XVIRTUALSCREEN),
               GetSystemMetrics(SM_YVIRTUALSCREEN), SRCCOPY);
    } else {
        HWND h = FindWindowW(L"MiniWeatherWnd", NULL);
        if (!h) {
            printf("window not found\n");
            return 1;
        }
        RECT r;
        if (!GetWindowRect(h, &r)) return 1;
        w = r.right - r.left;
        ht = r.bottom - r.top;
        if (w <= 0 || ht <= 0) return 1;
        mdc = CreateCompatibleDC(sdc);
        bmp = CreateCompatibleBitmap(sdc, w, ht);
        old = SelectObject(mdc, bmp);
        if (front) {
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            SetForegroundWindow(h);
            Sleep(500);
            RECT nr;
            GetWindowRect(h, &nr);
            BitBlt(mdc, 0, 0, w, ht, sdc, nr.left, nr.top, SRCCOPY);
        } else if (!PrintWindow(h, mdc, 2)) {
            PrintWindow(h, mdc, 0);
        }
    }

    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -ht;      /* 自顶向下 */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    unsigned char *buf = (unsigned char *)malloc((size_t)w * (size_t)ht * 4);
    if (!buf) return 1;
    if (!GetDIBits(mdc, bmp, 0, (UINT)ht, buf, &bi, DIB_RGB_COLORS)) return 1;

    FILE *f = _wfopen(out, L"wb");
    if (!f) { free(buf); return 1; }
    fwrite(&w, 4, 1, f);
    fwrite(&ht, 4, 1, f);
    fwrite(buf, 1, (size_t)w * (size_t)ht * 4, f);
    fclose(f);

    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(NULL, sdc);
    free(buf);

    printf("saved %dx%d\n", w, ht);
    return 0;
}
