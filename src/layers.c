#include "vimpaint.h"

double blend_apply(BlendMode mode, double cb, double cs) {
  switch (mode) {
  case BLEND_MULTIPLY:
    return cb * cs;
  case BLEND_SCREEN:
    return cb + cs - cb * cs;
  case BLEND_OVERLAY:
    return cb <= 0.5 ? 2.0 * cb * cs : 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs);
  case BLEND_DARKEN:
    return cb < cs ? cb : cs;
  case BLEND_LIGHTEN:
    return cb > cs ? cb : cs;
  case BLEND_COLOR_DODGE:
    return cb == 0.0   ? 0.0
           : cs == 1.0 ? 1.0
                       : (cb / (1.0 - cs) < 1.0 ? cb / (1.0 - cs) : 1.0);
  case BLEND_COLOR_BURN:
    return cb == 1.0 ? 1.0
           : cs == 0.0
               ? 0.0
               : (1.0 - (1.0 - cb) / cs > 0.0 ? 1.0 - (1.0 - cb) / cs : 0.0);
  case BLEND_HARD_LIGHT:
    return cs <= 0.5 ? 2.0 * cb * cs : 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs);
  case BLEND_SOFT_LIGHT: {
    double d = cb <= 0.25 ? ((16.0 * cb - 12.0) * cb + 4.0) * cb : sqrt(cb);
    return cs <= 0.5 ? cb - (1.0 - 2.0 * cs) * cb * (1.0 - cb)
                     : cb + (2.0 * cs - 1.0) * (d - cb);
  }
  case BLEND_DIFFERENCE:
    return cb > cs ? cb - cs : cs - cb;
  case BLEND_EXCLUSION:
    return cb + cs - 2.0 * cb * cs;
  default:
    return cs;
  }
}

/* Composite all visible layers (bottom to top) into dst buffer */
void layers_composite(guint32 *dst, int total) {
  memset(dst, 0, total * sizeof(guint32));
  for (int li = 0; li < layer_count; li++) {
    if (!layer_visible[li] || !layer_bufs[li])
      continue;
    BlendMode mode = layer_blend[li];
    double op = layer_opacity[li] / 100.0;
    for (int i = 0; i < total; i++) {
      guint32 src = layer_bufs[li][i];
      double sa = (src & 0xff) / 255.0 * op;
      if (sa == 0.0)
        continue;
      guint32 d = dst[i];
      double da = (d & 0xff) / 255.0;
      double ra = sa + da * (1.0 - sa);
      if (ra < 1e-6) {
        dst[i] = 0;
        continue;
      }
      double cs_r = (src >> 24 & 0xff) / 255.0;
      double cs_g = (src >> 16 & 0xff) / 255.0;
      double cs_b = (src >> 8 & 0xff) / 255.0;
      double cb_r = (d >> 24 & 0xff) / 255.0;
      double cb_g = (d >> 16 & 0xff) / 255.0;
      double cb_b = (d >> 8 & 0xff) / 255.0;
      double rr, rg, rb;
      if (mode == BLEND_NORMAL) {
        double inv = 1.0 - sa;
        rr = (cs_r * sa + cb_r * da * inv) / ra;
        rg = (cs_g * sa + cb_g * da * inv) / ra;
        rb = (cs_b * sa + cb_b * da * inv) / ra;
      } else {
        /* W3C compositing: Co = αs((1−αb)Cs + αb·B(Cb,Cs)) + αb(1−αs)Cb */
        rr = (sa * ((1.0 - da) * cs_r + da * blend_apply(mode, cb_r, cs_r)) +
              da * (1.0 - sa) * cb_r) /
             ra;
        rg = (sa * ((1.0 - da) * cs_g + da * blend_apply(mode, cb_g, cs_g)) +
              da * (1.0 - sa) * cb_g) /
             ra;
        rb = (sa * ((1.0 - da) * cs_b + da * blend_apply(mode, cb_b, cs_b)) +
              da * (1.0 - sa) * cb_b) /
             ra;
      }
      dst[i] = PACK_RGBA(CLAMP((int)(rr * 255.0 + 0.5), 0, 255),
                         CLAMP((int)(rg * 255.0 + 0.5), 0, 255),
                         CLAMP((int)(rb * 255.0 + 0.5), 0, 255),
                         CLAMP((int)(ra * 255.0 + 0.5), 0, 255));
    }
  }
}

/* Flatten all layers into layer 0 and reset to single layer. */
void layers_flatten(void) {
  if (layer_count <= 1) {
    layer_visible[0] = TRUE;
    return;
  }
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *flat = calloc(total, sizeof(guint32));
  if (flat)
    layers_composite(flat, total);
  for (int li = 0; li < layer_count; li++) {
    free(layer_bufs[li]);
    layer_bufs[li] = NULL;
  }
  layer_bufs[0] = flat ? flat : calloc(total, sizeof(guint32));
  pixels = layer_bufs[0];
  layer_count = 1;
  layer_active = 0;
  layer_visible[0] = TRUE;
  layer_opacity[0] = 100;
  layer_blend[0] = BLEND_NORMAL;
  snprintf(layer_name[0], 32, "Layer 1");
}
