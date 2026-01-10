#ifndef VIMPAINT_PALETTE_H
#define VIMPAINT_PALETTE_H

#include "state.h"

int palette_reserve(int needed);
void palette_to_rgb(int idx, int *r, int *g, int *b);
void set_palette_rgb(int slot, unsigned int rgb);
gboolean exec_palette(const char *cmd, const char *arg);

#endif
