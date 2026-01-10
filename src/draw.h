#ifndef VIMPAINT_DRAW_H
#define VIMPAINT_DRAW_H

#include "state.h"

void insert_paint(void);
void draw_line(int x0, int y0, int x1, int y1, guint32 color);
void paint_pixel_raw(int x, int y, guint32 color);
void paint_pixel(int x, int y, guint32 color);
void paint_brush(int x, int y, guint32 color);
guint32 fill_color_at(int x, int y, guint32 fg);
void flood_fill(int sx, int sy, guint32 fill_color);
void apply_gradient_linear(int x0, int y0, int x1, int y1, guint32 c1,
                           guint32 c2);

#endif
