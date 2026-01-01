#ifndef VIMPAINT_MOUSE_H
#define VIMPAINT_MOUSE_H

#include "state.h"

gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data);

#endif
