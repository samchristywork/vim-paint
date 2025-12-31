#ifndef VIMPAINT_MAIN_H
#define VIMPAINT_MAIN_H

#include "state.h"

void zoom_resize(void);
void tab_reset(void);
void flash_color(int idx);
void update_title(const char *filename);
void title_refresh(void);
void tab_save(int idx);
void tab_switch(int newidx);
void tab_close_current(void);
void cmd_set(const char *text);
void status_update(void);
gboolean on_flash_expire(gpointer data);
void cmd_flash(const char *text);
void usage(const char *prog, int exitcode);
int main(int argc, char *argv[]);
void exec_quit(const char *arg);
void exec_force_quit(const char *arg);
void exec_tabnew(const char *arg);
void exec_new(const char *arg);

#endif
