#ifndef VIMPAINT_IO_H
#define VIMPAINT_IO_H

#include "state.h"

gboolean cmd_write(const char *filename);
void cmd_export_bmp(const char *filename, int scale);
void cmd_open(const char *filename);
gboolean exec_io(const char *cmd, const char *arg);

#endif
