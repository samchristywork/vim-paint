#ifndef VIMPAINT_IO_H
#define VIMPAINT_IO_H

#include "state.h"

gboolean cmd_write(const char *filename);
void cmd_export_bmp(const char *filename, int scale);
void cmd_open(const char *filename);
void exec_write(const char *arg);
void exec_write_quit(const char *arg);
void exec_edit(const char *arg);
void exec_export(const char *arg);

#endif
