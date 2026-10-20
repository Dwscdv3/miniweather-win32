#ifndef UI_H
#define UI_H

#include "app.h"

/* 标签栏 / 按钮的几何布局，绘制与命中检测共用同一份计算 */
typedef struct {
    RECT tabs[MAX_CITIES];
    RECT close[MAX_CITIES];
    RECT add;
    RECT refresh;
    RECT row;
    int  ntab;
} UiLayout;

void   ui_init(void);
void   ui_free(void);
void   ui_fonts_recreate(void);
void   ui_layout(int cw, UiLayout *out);
void   ui_paint(HDC hdc, int cw, int ch);
HBRUSH ui_edit_brush(void);
HFONT  ui_edit_font(void);

#endif
