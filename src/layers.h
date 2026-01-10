#ifndef VIMPAINT_LAYERS_H
#define VIMPAINT_LAYERS_H

#include "state.h"

double blend_apply(BlendMode mode, double cb, double cs);
void layers_composite(guint32 *dst, int total);
void layers_flatten(void);
gboolean exec_layers(const char *cmd, const char *arg);

#endif
