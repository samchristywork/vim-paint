#ifndef VIMPAINT_EFFECTS_H
#define VIMPAINT_EFFECTS_H

#include "state.h"

void exec_replace(const char *arg);
void exec_gradient(const char *arg);
void exec_gradtool(const char *arg);
void exec_brushdefine(const char *arg);
void exec_hsl(const char *arg);
void exec_dither(const char *arg);
void exec_invert(const char *arg);
void exec_blur(const char *arg);

#endif
