#ifndef VIMPAINT_LAYERS_H
#define VIMPAINT_LAYERS_H

#include "state.h"

double blend_apply(BlendMode mode, double cb, double cs);
void layers_composite(guint32 *dst, int total);
void layers_flatten(void);
void exec_newlayer(const char *arg);
void exec_seltolay(const char *arg);
void exec_mergedown(const char *arg);
void exec_layer(const char *arg);
void exec_layervis(const char *arg);
void exec_layerblend(const char *arg);
void exec_layeropacity(const char *arg);

#endif
