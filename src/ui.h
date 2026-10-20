#ifndef UI_H
#define UI_H

#include "app.h"

void   ui_init(void);
void   ui_free(void);
void   ui_fonts_recreate(void);
void   ui_paint(HDC hdc, int cw, int ch);
RECT   ui_city_rect(int cw);
HBRUSH ui_edit_brush(void);
HFONT  ui_edit_font(void);

#endif
