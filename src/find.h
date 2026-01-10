#ifndef VIMPAINT_FIND_H
#define VIMPAINT_FIND_H

#include "state.h"

void find_right(void);
void find_left(void);
void find_down(void);
void find_up(void);
int snap_coord(int coord, gboolean horizontal);
gboolean exec_find(const char *cmd, const char *arg);

#endif
