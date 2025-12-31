#ifndef VIMPAINT_INPUT_H
#define VIMPAINT_INPUT_H

#include "state.h"

gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data);
gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data);

#endif
