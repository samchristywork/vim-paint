#ifndef VIMPAINT_EFFECTS_H
#define VIMPAINT_EFFECTS_H

#include "state.h"

void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l);
void hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b);
gboolean exec_effects(const char *cmd, const char *arg);

#endif
