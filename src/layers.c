#include "layers.h"
#include "main.h"
#include "undo.h"

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

static void newlayer(const char *arg) {
  (void)arg;
  if (layer_count >= LAYER_MAX) {
    cmd_flash("Layer limit reached.");
    return;
  }
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *buf = calloc(total, sizeof(guint32));
  if (!buf) {
    cmd_flash("Out of memory.");
    return;
  }
  int ins = layer_active + 1;
  for (int li = layer_count; li > ins; li--) {
    layer_bufs[li] = layer_bufs[li - 1];
    layer_visible[li] = layer_visible[li - 1];
    layer_blend[li] = layer_blend[li - 1];
    layer_opacity[li] = layer_opacity[li - 1];
    memcpy(layer_name[li], layer_name[li - 1], 32);
  }
  layer_bufs[ins] = buf;
  layer_visible[ins] = TRUE;
  layer_blend[ins] = BLEND_NORMAL;
  layer_opacity[ins] = 100;
  snprintf(layer_name[ins], 32, "Layer %d", layer_count + 1);
  layer_count++;
  layer_active = ins;
  pixels = layer_bufs[layer_active];
  clear_history();
  status_update();
  gtk_widget_queue_draw(main_canvas);
  char msg[64];
  snprintf(msg, sizeof(msg), "New layer %d/%d", layer_active + 1, layer_count);
  cmd_flash(msg);
}

static void seltolay(const char *arg) {
  (void)arg;
  if (!visual_mode) {
    cmd_flash("No selection. Enter visual mode first.");
    return;
  }
  if (layer_count >= LAYER_MAX) {
    cmd_flash("Layer limit reached.");
    return;
  }
  int x0 = MIN(cursor_x, visual_anchor_x);
  int x1 = MAX(cursor_x, visual_anchor_x);
  int y0 = MIN(cursor_y, visual_anchor_y);
  int y1 = MAX(cursor_y, visual_anchor_y);
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *buf = calloc(total, sizeof(guint32));
  if (!buf) {
    cmd_flash("Out of memory.");
    return;
  }
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      buf[y * CANVAS_W + x] = PX(y, x);
  int ins = layer_active + 1;
  for (int li = layer_count; li > ins; li--) {
    layer_bufs[li] = layer_bufs[li - 1];
    layer_visible[li] = layer_visible[li - 1];
    layer_blend[li] = layer_blend[li - 1];
    layer_opacity[li] = layer_opacity[li - 1];
    memcpy(layer_name[li], layer_name[li - 1], 32);
  }
  layer_bufs[ins] = buf;
  layer_visible[ins] = TRUE;
  layer_blend[ins] = BLEND_NORMAL;
  layer_opacity[ins] = 100;
  snprintf(layer_name[ins], 32, "Layer %d", layer_count + 1);
  layer_count++;
  layer_active = ins;
  pixels = layer_bufs[layer_active];
  visual_mode = FALSE;
  clear_history();
  status_update();
  gtk_widget_queue_draw(main_canvas);
  char msg[64];
  snprintf(msg, sizeof(msg), "Selection copied to layer %d/%d",
           layer_active + 1, layer_count);
  cmd_flash(msg);
}

static void mergedown(const char *arg) {
  (void)arg;
  if (layer_active == 0) {
    cmd_flash("Already at bottom layer.");
    return;
  }
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *dst = layer_bufs[layer_active - 1];
  guint32 *src = layer_bufs[layer_active];
  guint32 *before = malloc(total * sizeof(guint32));
  if (!before) {
    cmd_flash("Out of memory.");
    return;
  }
  memcpy(before, dst, total * sizeof(guint32));
  for (int i = 0; i < (int)total; i++) {
    guint32 s = src[i];
    double sa = (s & 0xff) / 255.0;
    if (sa == 0.0)
      continue;
    guint32 d = dst[i];
    double da = (d & 0xff) / 255.0;
    double ra = sa + da * (1.0 - sa);
    if (ra < 1e-6) {
      dst[i] = 0;
      continue;
    }
    double inv = 1.0 - sa;
    int rr = (int)(((s >> 24 & 0xff) / 255.0 * sa +
                    (d >> 24 & 0xff) / 255.0 * da * inv) /
                       ra * 255 +
                   0.5);
    int rg = (int)(((s >> 16 & 0xff) / 255.0 * sa +
                    (d >> 16 & 0xff) / 255.0 * da * inv) /
                       ra * 255 +
                   0.5);
    int rb = (int)(((s >> 8 & 0xff) / 255.0 * sa +
                    (d >> 8 & 0xff) / 255.0 * da * inv) /
                       ra * 255 +
                   0.5);
    dst[i] =
        PACK_RGBA(CLAMP(rr, 0, 255), CLAMP(rg, 0, 255), CLAMP(rb, 0, 255),
                  CLAMP((int)(ra * 255 + 0.5), 0, 255));
  }
  free(layer_bufs[layer_active]);
  for (int li = layer_active; li < layer_count - 1; li++) {
    layer_bufs[li] = layer_bufs[li + 1];
    layer_visible[li] = layer_visible[li + 1];
    layer_blend[li] = layer_blend[li + 1];
    layer_opacity[li] = layer_opacity[li + 1];
    memcpy(layer_name[li], layer_name[li + 1], 32);
  }
  layer_count--;
  layer_bufs[layer_count] = NULL;
  layer_visible[layer_count] = FALSE;
  layer_blend[layer_count] = BLEND_NORMAL;
  layer_opacity[layer_count] = 100;
  layer_active--;
  pixels = layer_bufs[layer_active];
  commit_canvas_snapshot(before, CANVAS_W, CANVAS_H);
  status_update();
  gtk_widget_queue_draw(main_canvas);
  cmd_flash("Merged down.");
}

static void layer(const char *arg) {
  int n = atoi(arg) - 1;
  if (n < 0 || n >= layer_count) {
    cmd_flash("Invalid layer number.");
    return;
  }
  layer_active = n;
  pixels = layer_bufs[layer_active];
  clear_history();
  status_update();
  gtk_widget_queue_draw(main_canvas);
  char msg[64];
  snprintf(msg, sizeof(msg), "Layer %d/%d", layer_active + 1, layer_count);
  cmd_flash(msg);
}

static void layervis(const char *arg) {
  int n = atoi(arg) - 1;
  if (n < 0 || n >= layer_count) {
    cmd_flash("Invalid layer number.");
    return;
  }
  layer_visible[n] = !layer_visible[n];
  gtk_widget_queue_draw(main_canvas);
  char msg[64];
  snprintf(msg, sizeof(msg), "Layer %d %s", n + 1,
           layer_visible[n] ? "visible" : "hidden");
  cmd_flash(msg);
}

static void layerblend(const char *arg) {
  BlendMode found = BLEND_MODE_COUNT;
  for (int m = 0; m < BLEND_MODE_COUNT; m++) {
    if (strcasecmp(arg, blend_mode_names[m]) == 0) {
      found = m;
      break;
    }
  }
  if (found == BLEND_MODE_COUNT) {
    char modes[256] = "";
    for (int m = 0; m < BLEND_MODE_COUNT; m++) {
      if (m)
        strncat(modes, "|", sizeof(modes) - strlen(modes) - 1);
      strncat(modes, blend_mode_names[m], sizeof(modes) - strlen(modes) - 1);
    }
    char emsg[320];
    snprintf(emsg, sizeof(emsg), "Unknown mode. Use: %s", modes);
    cmd_flash(emsg);
    return;
  }
  layer_blend[layer_active] = found;
  gtk_widget_queue_draw(main_canvas);
  char msg[64];
  snprintf(msg, sizeof(msg), "Layer %d blend: %s", layer_active + 1,
           blend_mode_names[found]);
  cmd_flash(msg);
}

static void layeropacity(const char *arg) {
  int v = atoi(arg);
  if (v < 0 || v > 100) {
    cmd_flash("Opacity must be 0-100.");
    return;
  }
  layer_opacity[layer_active] = v;
  gtk_widget_queue_draw(main_canvas);
  status_update();
  char msg[64];
  snprintf(msg, sizeof(msg), "Layer %d opacity: %d%%", layer_active + 1, v);
  cmd_flash(msg);
}

gboolean exec_layers(const char *cmd, const char *arg) {
  if (strcmp(cmd, ":newlayer") == 0)                      { newlayer(arg);     return TRUE; }
  if (strcmp(cmd, ":seltolay") == 0)                      { seltolay(arg);     return TRUE; }
  if (strcmp(cmd, ":mergedown") == 0)                     { mergedown(arg);    return TRUE; }
  if (strncmp(cmd, ":layer ", 7) == 0 && *arg)            { layer(arg);        return TRUE; }
  if (strncmp(cmd, ":layervis ", 10) == 0 && *arg)        { layervis(arg);     return TRUE; }
  if (strncmp(cmd, ":layerblend ", 12) == 0 && *arg)      { layerblend(arg);   return TRUE; }
  if (strncmp(cmd, ":layeropacity ", 14) == 0 && *arg)    { layeropacity(arg); return TRUE; }
  return FALSE;
}
