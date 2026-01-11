#ifndef VIMPAINT_COMMANDS_H
#define VIMPAINT_COMMANDS_H

#include "state.h"

void cmd_execute(void);
gboolean cmdline_key(GtkWidget *widget, GdkEventKey *event, gpointer data);

#endif
