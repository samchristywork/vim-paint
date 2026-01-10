#ifndef VIMPAINT_RENDER_H
#define VIMPAINT_RENDER_H

#include "state.h"

gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_palette_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_palette_click(GtkWidget *widget, GdkEventButton *event,
                          gpointer data);

#endif
