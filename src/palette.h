#ifndef VIMPAINT_PALETTE_H
#define VIMPAINT_PALETTE_H

#include "state.h"

int palette_reserve(int needed);
void palette_to_rgb(int idx, int *r, int *g, int *b);
void set_palette_rgb(int slot, unsigned int rgb);
void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l);
double hue_to_rgb(double p, double q, double t);
void hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b);
gboolean parse_color(const char *val, unsigned int *out_rgb);
gboolean exec_palette(const char *cmd, const char *arg);

#endif
