#include <glob.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CANVAS_W 80
#define DEFAULT_CANVAS_H 40
static int CANVAS_W = DEFAULT_CANVAS_W;
static int CANVAS_H = DEFAULT_CANVAS_H;
static int CELL_SIZE = 12;

/* palette: index 0 = background (white), 1..N = foreground colors.
   Pixel indices are stored as guchar, so the effective ceiling is 256. */
static double (*palette)[3] = NULL;
static int palette_size = 0;
static int palette_cap = 0;
#define PALETTE_SIZE palette_size

static int palette_reserve(int needed) {
  if (needed <= palette_cap)
    return 1;
  int nc = palette_cap ? palette_cap * 2 : 8;
  while (nc < needed)
    nc *= 2;
  double (*np)[3] = realloc(palette, (size_t)nc * sizeof(*np));
  if (!np)
    return 0;
  palette = np;
  palette_cap = nc;
  return 1;
}

#define PACK_RGBA(r, g, b, a)                                                  \
  (((guint32)(r) << 24) | ((guint32)(g) << 16) | ((guint32)(b) << 8) |         \
   (guint32)(a))

static guint32 *pixels = NULL; /* flat [y * CANVAS_W + x], RRGGBBAA format */
static guint32 fg_color =
    0x000000ff; /* current foreground color (RGBA, opaque black) */
static guint32 bg_color =
    0xffffffff; /* background / erase color (RGBA, opaque white) */

#define PX(y, x) pixels[(y) * CANVAS_W + (x)]
#define RECT_BOUNDS(rad)                                                       \
  int x0 = CLAMP(cursor_x - (rad), 0, CANVAS_W - 1);                           \
  int x1 = CLAMP(cursor_x + (rad), 0, CANVAS_W - 1);                           \
  int y0 = CLAMP(cursor_y - (rad), 0, CANVAS_H - 1);                           \
  int y1 = CLAMP(cursor_y + (rad), 0, CANVAS_H - 1)
static int cursor_x = 0;
static int cursor_y = 0;
static gboolean visual_mode = FALSE;
static int visual_anchor_x = 0;
static int visual_anchor_y = 0;
static gboolean insert_mode = FALSE;
static gboolean show_grid = TRUE;
static guint32 grid_color = 0xccccccff; /* RRGGBBAA */
static gboolean show_ruler = FALSE;
static gboolean show_checker = FALSE;
static gboolean gradient_tool = FALSE;
static gboolean grad_dragging = FALSE;
static int grad_x0 = 0, grad_y0 = 0;
static guint32 grad_c1 = 0, grad_c2 = 0;

#define GUIDE_MAX 64
typedef struct { int coord; gboolean horizontal; } Guide;
static Guide guides[GUIDE_MAX];
static int guide_count = 0;
static gboolean guide_snap = FALSE;
static gboolean canvas_dirty = FALSE;
typedef enum { SYM_NONE = 0, SYM_H, SYM_V, SYM_HV, SYM_RADIAL } SymMode;
static SymMode sym_mode = SYM_NONE;
static int sym_radial_n = 4;
static int brush_size = 1;
static int brush_shape = 0;    /* 0 = square, 1 = circle, 2 = custom */
#define CUSTOM_BRUSH_MAX 16
static int custom_brush_w = 0, custom_brush_h = 0;
static gboolean custom_brush_pixels[CUSTOM_BRUSH_MAX][CUSTOM_BRUSH_MAX];
static int spray_density = 0;  /* 0 = off, 1-100 = % of pixels painted */

typedef enum {
  FILL_SOLID = 0,
  FILL_CHECKER,
  FILL_HSTRIPES,
  FILL_VSTRIPES,
} FillPattern;
static FillPattern fill_pattern = FILL_SOLID;
static char text_font_family[256] = "Monospace";
static double text_font_size = 10.0;

#define UNDO_MAX 256
static int undo_levels = 64;
typedef struct {
  int x, y;
  guint32 before, after;
} PixelChange;
typedef struct {
  PixelChange *changes;
  int count;
  /* Non-NULL for canvas-resizing ops */
  guint32 *before_snap;
  int before_w, before_h;
  guint32 *after_snap;
  int after_w, after_h;
} UndoAction;
static PixelChange *staged = NULL;
static int staged_count = 0, staged_cap = 0;
static UndoAction undo_stack[UNDO_MAX];
static int undo_top = 0, undo_count = 0;
static UndoAction redo_stack[UNDO_MAX];
static int redo_top = 0, redo_count = 0;

#define LAYER_MAX 8
static guint32 *layer_bufs[LAYER_MAX];
static gboolean layer_visible[LAYER_MAX];
static char layer_name[LAYER_MAX][32];
static int layer_count = 1;
static int layer_active = 0;

typedef enum {
  BLEND_NORMAL = 0,
  BLEND_MULTIPLY,
  BLEND_SCREEN,
  BLEND_OVERLAY,
  BLEND_DARKEN,
  BLEND_LIGHTEN,
  BLEND_COLOR_DODGE,
  BLEND_COLOR_BURN,
  BLEND_HARD_LIGHT,
  BLEND_SOFT_LIGHT,
  BLEND_DIFFERENCE,
  BLEND_EXCLUSION,
  BLEND_MODE_COUNT
} BlendMode;

static BlendMode layer_blend[LAYER_MAX];

static const char *blend_mode_names[] = {
  "normal", "multiply", "screen", "overlay", "darken", "lighten",
  "color-dodge", "color-burn", "hard-light", "soft-light",
  "difference", "exclusion"
};

static double blend_apply(BlendMode mode, double cb, double cs) {
  switch (mode) {
    case BLEND_MULTIPLY:    return cb * cs;
    case BLEND_SCREEN:      return cb + cs - cb * cs;
    case BLEND_OVERLAY:     return cb <= 0.5 ? 2.0*cb*cs : 1.0 - 2.0*(1.0-cb)*(1.0-cs);
    case BLEND_DARKEN:      return cb < cs ? cb : cs;
    case BLEND_LIGHTEN:     return cb > cs ? cb : cs;
    case BLEND_COLOR_DODGE: return cb == 0.0 ? 0.0 : cs == 1.0 ? 1.0 : (cb/(1.0-cs) < 1.0 ? cb/(1.0-cs) : 1.0);
    case BLEND_COLOR_BURN:  return cb == 1.0 ? 1.0 : cs == 0.0 ? 0.0 : (1.0 - (1.0-cb)/cs > 0.0 ? 1.0 - (1.0-cb)/cs : 0.0);
    case BLEND_HARD_LIGHT:  return cs <= 0.5 ? 2.0*cb*cs : 1.0 - 2.0*(1.0-cb)*(1.0-cs);
    case BLEND_SOFT_LIGHT: {
      double d = cb <= 0.25 ? ((16.0*cb - 12.0)*cb + 4.0)*cb : sqrt(cb);
      return cs <= 0.5 ? cb - (1.0-2.0*cs)*cb*(1.0-cb) : cb + (2.0*cs-1.0)*(d-cb);
    }
    case BLEND_DIFFERENCE:  return cb > cs ? cb - cs : cs - cb;
    case BLEND_EXCLUSION:   return cb + cs - 2.0*cb*cs;
    default:                return cs;
  }
}

/* Composite all visible layers (bottom to top) into dst buffer */
static void layers_composite(guint32 *dst, int total) {
  memset(dst, 0, total * sizeof(guint32));
  for (int li = 0; li < layer_count; li++) {
    if (!layer_visible[li] || !layer_bufs[li])
      continue;
    BlendMode mode = layer_blend[li];
    for (int i = 0; i < total; i++) {
      guint32 src = layer_bufs[li][i];
      double sa = (src & 0xff) / 255.0;
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
      double cs_b = (src >>  8 & 0xff) / 255.0;
      double cb_r = (d >> 24 & 0xff) / 255.0;
      double cb_g = (d >> 16 & 0xff) / 255.0;
      double cb_b = (d >>  8 & 0xff) / 255.0;
      double rr, rg, rb;
      if (mode == BLEND_NORMAL) {
        double inv = 1.0 - sa;
        rr = (cs_r * sa + cb_r * da * inv) / ra;
        rg = (cs_g * sa + cb_g * da * inv) / ra;
        rb = (cs_b * sa + cb_b * da * inv) / ra;
      } else {
        /* W3C compositing: Co = αs((1−αb)Cs + αb·B(Cb,Cs)) + αb(1−αs)Cb */
        rr = (sa * ((1.0-da)*cs_r + da*blend_apply(mode, cb_r, cs_r)) + da*(1.0-sa)*cb_r) / ra;
        rg = (sa * ((1.0-da)*cs_g + da*blend_apply(mode, cb_g, cs_g)) + da*(1.0-sa)*cb_g) / ra;
        rb = (sa * ((1.0-da)*cs_b + da*blend_apply(mode, cb_b, cs_b)) + da*(1.0-sa)*cb_b) / ra;
      }
      dst[i] = PACK_RGBA(CLAMP((int)(rr*255.0+0.5), 0, 255),
                         CLAMP((int)(rg*255.0+0.5), 0, 255),
                         CLAMP((int)(rb*255.0+0.5), 0, 255),
                         CLAMP((int)(ra*255.0+0.5), 0, 255));
    }
  }
}

/* Flatten all layers into layer 0 and reset to single layer. */
static void layers_flatten(void) {
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
  snprintf(layer_name[0], 32, "Layer 1");
}

#define TAB_MAX 8
typedef struct {
  guint32 *pixels;
  int w, h, cx, cy;
  char filename[4096];
  gboolean dirty;
  UndoAction undo_stack[UNDO_MAX];
  int undo_top, undo_count;
  UndoAction redo_stack[UNDO_MAX];
  int redo_top, redo_count;
} TabState;
static TabState tabs[TAB_MAX];
static int tab_count = 1;
static int tab_current = 0;

static gboolean cmd_mode = FALSE;
static char cmd_buf[4096];
static int cmd_len = 0;

#define MACRO_MAX_EVENTS 4096
typedef struct {
  guint keyval;
  GdkModifierType state;
} MacroEvent;
static MacroEvent macro_buf[26][MACRO_MAX_EVENTS];
static int macro_len[26];
static gboolean macro_recording = FALSE;
static int macro_reg = -1;
static gboolean macro_playing = FALSE;

#define CMD_HISTORY_MAX 64
static char cmd_history[CMD_HISTORY_MAX][4096];
static int cmd_history_count = 0;
static int cmd_history_idx = -1;
static char cmd_history_draft[4096] = "";

static glob_t tab_glob;
static gboolean tab_glob_valid = FALSE;
static int tab_glob_idx = 0;
static char tab_cmd_prefix[32];

static int color_tab_matches[256];
static int color_tab_count = 0;
static int color_tab_idx = 0;
static gboolean color_tab_valid = FALSE;
static char color_tab_prefix[64];
static char last_filename[4096] = "";
static GtkWidget *cmd_label = NULL;
static GtkWidget *main_canvas = NULL;
static GtkWidget *palette_bar = NULL;
static GtkWidget *main_window = NULL;

#define SWATCH_W 24
#define SWATCH_H 20

static void zoom_resize(void) {
  gtk_widget_set_size_request(main_canvas, CANVAS_W * CELL_SIZE,
                              CANVAS_H * CELL_SIZE);
  gtk_widget_set_size_request(palette_bar, CANVAS_W * CELL_SIZE, SWATCH_H);
  gtk_widget_set_size_request(cmd_label, CANVAS_W * CELL_SIZE, 20);
  gtk_window_resize(GTK_WINDOW(main_window), 1, 1);
}

static void tab_reset(void) {
  if (tab_glob_valid) {
    globfree(&tab_glob);
    tab_glob_valid = FALSE;
  }
  tab_glob_idx = 0;
  color_tab_valid = FALSE;
  color_tab_count = 0;
  color_tab_idx = 0;
}

static void palette_to_rgb(int idx, int *r, int *g, int *b);
static void draw_line(int x0, int y0, int x1, int y1, guint32 color);
static void insert_paint(void);
static void cmd_flash(const char *text);
static void clear_history(void);
static void begin_undo_action(void);
static void push_undo(int x, int y);
static void commit_undo_action(void);
static void commit_canvas_snapshot(guint32 *before_snap, int bw, int bh);
static void cmd_open(const char *filename);
static void cmd_set(const char *text);
static void title_refresh(void);
static void status_update(void);
static void paint_pixel(int x, int y, guint32 color);
static void paint_brush(int x, int y, guint32 color);
static void tab_switch(int newidx);

static void flash_color(int idx) {
  char buf[64];
  int r, g, b;
  palette_to_rgb(idx, &r, &g, &b);
  snprintf(buf, sizeof(buf), "color %d  #%02x%02x%02x", idx, r, g, b);
  cmd_flash(buf);
}

static void update_title(const char *filename) {
  char buf[4300];
  snprintf(buf, sizeof(buf), "vim-paint - %s%s  %dx%d  (%d,%d)", filename,
           canvas_dirty ? " [+]" : "", CANVAS_W, CANVAS_H, cursor_x + 1,
           cursor_y + 1);
  gtk_window_set_title(GTK_WINDOW(main_window), buf);
}

static void title_refresh(void) {
  char buf[512];
  char tab_info[20] = "";
  if (tab_count > 1)
    snprintf(tab_info, sizeof(tab_info), "[%d/%d] ", tab_current + 1,
             tab_count);
  if (*last_filename)
    snprintf(buf, sizeof(buf), "vim-paint %s- %s%s  %dx%d  (%d,%d)", tab_info,
             last_filename, canvas_dirty ? " [+]" : "", CANVAS_W, CANVAS_H,
             cursor_x + 1, cursor_y + 1);
  else
    snprintf(buf, sizeof(buf), "vim-paint %s%s  %dx%d  (%d,%d)", tab_info,
             canvas_dirty ? "[+] " : "", CANVAS_W, CANVAS_H, cursor_x + 1,
             cursor_y + 1);
  gtk_window_set_title(GTK_WINDOW(main_window), buf);
}

static void tab_save(int idx) {
  layers_flatten();
  size_t sz = (size_t)(size_t)CANVAS_W * CANVAS_H * sizeof(guint32);
  if (!tabs[idx].pixels || tabs[idx].w != CANVAS_W || tabs[idx].h != CANVAS_H) {
    free(tabs[idx].pixels);
    tabs[idx].pixels = malloc(sz);
    if (!tabs[idx].pixels)
      return;
  }
  memcpy(tabs[idx].pixels, pixels, sz);
  tabs[idx].w = CANVAS_W;
  tabs[idx].h = CANVAS_H;
  tabs[idx].cx = cursor_x;
  tabs[idx].cy = cursor_y;
  tabs[idx].dirty = canvas_dirty;
  snprintf(tabs[idx].filename, sizeof(tabs[idx].filename), "%s", last_filename);
  /* Transfer undo/redo ownership from globals to tab storage */
  memcpy(tabs[idx].undo_stack, undo_stack, sizeof(undo_stack));
  tabs[idx].undo_top = undo_top;
  tabs[idx].undo_count = undo_count;
  memcpy(tabs[idx].redo_stack, redo_stack, sizeof(redo_stack));
  tabs[idx].redo_top = redo_top;
  tabs[idx].redo_count = redo_count;
  memset(undo_stack, 0, sizeof(undo_stack));
  undo_top = 0;
  undo_count = 0;
  memset(redo_stack, 0, sizeof(redo_stack));
  redo_top = 0;
  redo_count = 0;
  staged_count = 0;
}

static void tab_switch(int newidx) {
  if (newidx < 0 || newidx >= tab_count || newidx == tab_current)
    return;
  macro_recording = FALSE;
  tab_save(tab_current);
  tab_current = newidx;
  TabState *t = &tabs[tab_current];
  guint32 *np = malloc(t->w * t->h * sizeof(guint32));
  if (!np)
    return;
  memcpy(np, t->pixels, t->w * t->h * sizeof(guint32));
  free(pixels);
  pixels = np;
  layer_bufs[0] = pixels;
  layer_count = 1;
  layer_active = 0;
  layer_visible[0] = TRUE;
  snprintf(layer_name[0], 32, "Layer 1");
  CANVAS_W = t->w;
  CANVAS_H = t->h;
  cursor_x = CLAMP(t->cx, 0, CANVAS_W - 1);
  cursor_y = CLAMP(t->cy, 0, CANVAS_H - 1);
  canvas_dirty = t->dirty;
  visual_mode = FALSE;
  insert_mode = FALSE;
  snprintf(last_filename, sizeof(last_filename), "%s", t->filename);
  /* Restore undo/redo ownership from tab storage to globals */
  memcpy(undo_stack, t->undo_stack, sizeof(undo_stack));
  undo_top = t->undo_top;
  undo_count = t->undo_count;
  memcpy(redo_stack, t->redo_stack, sizeof(redo_stack));
  redo_top = t->redo_top;
  redo_count = t->redo_count;
  memset(t->undo_stack, 0, sizeof(t->undo_stack));
  t->undo_top = t->undo_count = 0;
  memset(t->redo_stack, 0, sizeof(t->redo_stack));
  t->redo_top = t->redo_count = 0;
  staged_count = 0;
  zoom_resize();
  title_refresh();
  status_update();
  gtk_widget_queue_draw(main_canvas);
}

static void tab_close_current(void) {
  /* Free the closing tab's undo history (currently in globals) */
  clear_history();
  free(tabs[tab_current].pixels);
  tabs[tab_current].pixels = NULL;
  /* Shift remaining tabs down; zero the vacated trailing slot's undo pointers
     to avoid aliased ownership after the struct copy. */
  for (int i = tab_current; i < tab_count - 1; i++)
    tabs[i] = tabs[i + 1];
  memset(tabs[tab_count - 1].undo_stack, 0, sizeof(tabs[0].undo_stack));
  tabs[tab_count - 1].undo_top = tabs[tab_count - 1].undo_count = 0;
  memset(tabs[tab_count - 1].redo_stack, 0, sizeof(tabs[0].redo_stack));
  tabs[tab_count - 1].redo_top = tabs[tab_count - 1].redo_count = 0;
  tabs[tab_count - 1].pixels = NULL;
  tab_count--;
  if (tab_current >= tab_count)
    tab_current = tab_count - 1;
  TabState *t = &tabs[tab_current];
  guint32 *np = malloc(t->w * t->h * sizeof(guint32));
  if (!np)
    return;
  memcpy(np, t->pixels, t->w * t->h * sizeof(guint32));
  free(pixels);
  pixels = np;
  layer_bufs[0] = pixels;
  layer_count = 1;
  layer_active = 0;
  layer_visible[0] = TRUE;
  snprintf(layer_name[0], 32, "Layer 1");
  CANVAS_W = t->w;
  CANVAS_H = t->h;
  cursor_x = CLAMP(t->cx, 0, CANVAS_W - 1);
  cursor_y = CLAMP(t->cy, 0, CANVAS_H - 1);
  canvas_dirty = t->dirty;
  visual_mode = FALSE;
  insert_mode = FALSE;
  snprintf(last_filename, sizeof(last_filename), "%s", t->filename);
  /* Restore the new current tab's undo/redo history */
  memcpy(undo_stack, t->undo_stack, sizeof(undo_stack));
  undo_top = t->undo_top;
  undo_count = t->undo_count;
  memcpy(redo_stack, t->redo_stack, sizeof(redo_stack));
  redo_top = t->redo_top;
  redo_count = t->redo_count;
  memset(t->undo_stack, 0, sizeof(t->undo_stack));
  t->undo_top = t->undo_count = 0;
  memset(t->redo_stack, 0, sizeof(t->redo_stack));
  t->redo_top = t->redo_count = 0;
  staged_count = 0;
  zoom_resize();
  title_refresh();
  status_update();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

static gboolean on_palette_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  for (int i = 0; i < PALETTE_SIZE; i++) {
    cairo_set_source_rgb(cr, palette[i][0], palette[i][1], palette[i][2]);
    cairo_rectangle(cr, i * SWATCH_W, 0, SWATCH_W, SWATCH_H);
    cairo_fill(cr);
    /* Highlight the swatch whose color matches fg_color */
    int pr, pg, pb;
    palette_to_rgb(i, &pr, &pg, &pb);
    guint32 swatch_rgba = PACK_RGBA(pr, pg, pb, 255);
    if (swatch_rgba == fg_color) {
      cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
      cairo_set_line_width(cr, 2.0);
      cairo_rectangle(cr, i * SWATCH_W + 1, 1, SWATCH_W - 2, SWATCH_H - 2);
      cairo_stroke(cr);
    }
  }
  /* Fill remaining space with a neutral background */
  cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
  cairo_rectangle(cr, PALETTE_SIZE * SWATCH_W, 0, CANVAS_W * CELL_SIZE,
                  SWATCH_H);
  cairo_fill(cr);
  return FALSE;
}

static void cmd_set(const char *text) {
  gtk_label_set_text(GTK_LABEL(cmd_label), text);
}

static guint flash_timer_id = 0;

static void status_update(void) {
  if (cmd_mode)
    return;
  if (flash_timer_id)
    return;
  char buf[160];
  const char *mode = gradient_tool ? "GRADIENT" : visual_mode ? "VISUAL" : insert_mode ? "INSERT" : "NORMAL";
  guchar r = (fg_color >> 24) & 0xff;
  guchar g = (fg_color >> 16) & 0xff;
  guchar b = (fg_color >> 8) & 0xff;
  char layer_info[48] = "";
  if (layer_count > 1) {
    BlendMode bm = layer_blend[layer_active];
    if (bm != BLEND_NORMAL)
      snprintf(layer_info, sizeof(layer_info), "  L%d/%d[%s]",
               layer_active + 1, layer_count, blend_mode_names[bm]);
    else
      snprintf(layer_info, sizeof(layer_info), "  L%d/%d", layer_active + 1, layer_count);
  }
  char sym_info[24] = "";
  switch (sym_mode) {
    case SYM_H:      snprintf(sym_info, sizeof(sym_info), "  symH"); break;
    case SYM_V:      snprintf(sym_info, sizeof(sym_info), "  symV"); break;
    case SYM_HV:     snprintf(sym_info, sizeof(sym_info), "  sym4"); break;
    case SYM_RADIAL: snprintf(sym_info, sizeof(sym_info), "  sym%d", sym_radial_n); break;
    default: break;
  }
  if (macro_recording)
    snprintf(buf, sizeof(buf),
             " %s  recording @%c  col: %d  row: %d  #%02x%02x%02x  %dx%d  z%d%s%s",
             mode, 'a' + macro_reg, cursor_x + 1, cursor_y + 1, r, g, b,
             CANVAS_W, CANVAS_H, CELL_SIZE, layer_info, sym_info);
  else
    snprintf(buf, sizeof(buf),
             " %s  col: %d  row: %d  #%02x%02x%02x  %dx%d  z%d%s%s",
             mode, cursor_x + 1, cursor_y + 1, r, g, b, CANVAS_W, CANVAS_H,
             CELL_SIZE, layer_info, sym_info);
  gtk_label_set_text(GTK_LABEL(cmd_label), buf);
  title_refresh();
}

static gboolean on_flash_expire(gpointer data) {
  flash_timer_id = 0;
  status_update();
  return G_SOURCE_REMOVE;
}

static void cmd_flash(const char *text) {
  cmd_set(text);
  if (flash_timer_id)
    g_source_remove(flash_timer_id);
  flash_timer_id = g_timeout_add(2000, on_flash_expire, NULL);
}

static gboolean cmd_write(const char *filename) {
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *composite = malloc(total * sizeof(guint32));
  if (composite)
    layers_composite(composite, total);
  cairo_surface_t *surf =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, CANVAS_W, CANVAS_H);
  guchar *d = cairo_image_surface_get_data(surf);
  int stride = cairo_image_surface_get_stride(surf);
  for (int y = 0; y < CANVAS_H; y++)
    for (int x = 0; x < CANVAS_W; x++) {
      guint32 px = composite ? composite[y * CANVAS_W + x] : PX(y, x);
      guchar r = (px >> 24) & 0xff;
      guchar g = (px >> 16) & 0xff;
      guchar b = (px >> 8) & 0xff;
      guchar a = px & 0xff;
      d[y * stride + x * 4 + 0] = b; /* Cairo ARGB32: BGRA on LE */
      d[y * stride + x * 4 + 1] = g;
      d[y * stride + x * 4 + 2] = r;
      d[y * stride + x * 4 + 3] = a;
    }
  cairo_surface_mark_dirty(surf);
  gboolean ok =
      cairo_surface_write_to_png(surf, filename) == CAIRO_STATUS_SUCCESS;
  cairo_surface_destroy(surf);
  free(composite);
  if (ok) {
    canvas_dirty = FALSE;
    title_refresh();
    cmd_flash("Written.");
  } else {
    cmd_flash("Write failed.");
  }
  return ok;
}

static void cmd_export_bmp(const char *filename, int scale) {
  int out_w = CANVAS_W * scale, out_h = CANVAS_H * scale;
  int row_bytes = out_w * 3;
  int pad = (4 - (row_bytes % 4)) % 4;
  int stride = row_bytes + pad;
  int pixel_data_size = stride * out_h;
  int file_size = 14 + 40 + pixel_data_size;

  FILE *f = fopen(filename, "wb");
  if (!f) {
    cmd_flash("Export failed.");
    return;
  }

  /* File header */
  unsigned char fh[14] = {
      'B',
      'M',
      (unsigned char)(file_size),
      (unsigned char)(file_size >> 8),
      (unsigned char)(file_size >> 16),
      (unsigned char)(file_size >> 24),
      0,
      0,
      0,
      0, /* reserved */
      54,
      0,
      0,
      0, /* offset to pixel data */
  };
  /* Info header (BITMAPINFOHEADER) */
  int neg_h = -out_h; /* negative = top-down */
  unsigned char ih[40] = {
      40,
      0,
      0,
      0, /* header size */
      (unsigned char)(out_w),
      (unsigned char)(out_w >> 8),
      (unsigned char)(out_w >> 16),
      (unsigned char)(out_w >> 24),
      (unsigned char)(neg_h),
      (unsigned char)(neg_h >> 8),
      (unsigned char)(neg_h >> 16),
      (unsigned char)(neg_h >> 24),
      1,
      0, /* color planes */
      24,
      0, /* bits per pixel */
      0,
      0,
      0,
      0, /* compression (none) */
      (unsigned char)(pixel_data_size),
      (unsigned char)(pixel_data_size >> 8),
      (unsigned char)(pixel_data_size >> 16),
      (unsigned char)(pixel_data_size >> 24),
      0,
      0,
      0,
      0, /* x pixels/meter */
      0,
      0,
      0,
      0, /* y pixels/meter */
      0,
      0,
      0,
      0, /* colors in table */
      0,
      0,
      0,
      0, /* important colors */
  };

  fwrite(fh, 1, sizeof(fh), f);
  fwrite(ih, 1, sizeof(ih), f);

  unsigned char *row_buf = malloc(row_bytes);
  if (!row_buf) {
    fclose(f);
    cmd_flash("Export failed.");
    return;
  }
  unsigned char padding[3] = {0, 0, 0};
  for (int y = 0; y < CANVAS_H; y++) {
    for (int x = 0; x < CANVAS_W; x++) {
      guint32 px = PX(y, x);
      guchar r = (px >> 24) & 0xff;
      guchar g = (px >> 16) & 0xff;
      guchar b = (px >> 8) & 0xff;
      for (int xs = 0; xs < scale; xs++) {
        int ox = x * scale + xs;
        row_buf[ox * 3 + 0] = b; /* BMP: BGR order */
        row_buf[ox * 3 + 1] = g;
        row_buf[ox * 3 + 2] = r;
      }
    }
    for (int ys = 0; ys < scale; ys++) {
      fwrite(row_buf, 1, row_bytes, f);
      if (pad)
        fwrite(padding, 1, pad, f);
    }
  }
  free(row_buf);
  fclose(f);
  gboolean has_alpha = FALSE;
  for (int i = 0; i < CANVAS_W * CANVAS_H && !has_alpha; i++)
    if ((pixels[i] & 0xff) != 0xff)
      has_alpha = TRUE;
  if (has_alpha)
    cmd_flash("Exported (warning: transparency lost, BMP has no alpha).");
  else
    cmd_flash("Exported.");
}

// clang-format off
static const struct { const char *name; unsigned int rgb; } named_colors[] = {
    {"black", 0x000000}, {"navy", 0x000080}, {"darkblue", 0x00008b},
    {"mediumblue", 0x0000cd}, {"blue", 0x0000ff}, {"darkgreen", 0x006400},
    {"green", 0x008000}, {"teal", 0x008080}, {"darkcyan", 0x008b8b},
    {"deepskyblue", 0x00bfff}, {"darkturquoise", 0x00ced1}, {"mediumspringgreen", 0x00fa9a},
    {"lime", 0x00ff00}, {"springgreen", 0x00ff7f}, {"aqua", 0x00ffff},
    {"cyan", 0x00ffff}, {"midnightblue", 0x191970}, {"dodgerblue", 0x1e90ff},
    {"lightseagreen", 0x20b2aa}, {"forestgreen", 0x228b22}, {"seagreen", 0x2e8b57},
    {"darkslategray", 0x2f4f4f}, {"darkslategrey", 0x2f4f4f}, {"limegreen", 0x32cd32},
    {"mediumseagreen", 0x3cb371}, {"turquoise", 0x40e0d0}, {"royalblue", 0x4169e1},
    {"steelblue", 0x4682b4}, {"darkslateblue", 0x483d8b}, {"mediumturquoise", 0x48d1cc},
    {"indigo", 0x4b0082}, {"darkolivegreen", 0x556b2f}, {"cadetblue", 0x5f9ea0},
    {"cornflowerblue", 0x6495ed}, {"rebeccapurple", 0x663399}, {"mediumaquamarine", 0x66cdaa},
    {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"slateblue", 0x6a5acd},
    {"olivedrab", 0x6b8e23}, {"slategray", 0x708090}, {"slategrey", 0x708090},
    {"lightslategray", 0x778899}, {"lightslategrey", 0x778899}, {"mediumslateblue", 0x7b68ee},
    {"lawngreen", 0x7cfc00}, {"chartreuse", 0x7fff00}, {"aquamarine", 0x7fffd4},
    {"maroon", 0x800000}, {"purple", 0x800080}, {"olive", 0x808000},
    {"gray", 0x808080}, {"grey", 0x808080}, {"skyblue", 0x87ceeb},
    {"lightskyblue", 0x87cefa}, {"blueviolet", 0x8a2be2}, {"darkred", 0x8b0000},
    {"darkmagenta", 0x8b008b}, {"saddlebrown", 0x8b4513}, {"darkseagreen", 0x8fbc8f},
    {"lightgreen", 0x90ee90}, {"mediumpurple", 0x9370db}, {"darkviolet", 0x9400d3},
    {"palegreen", 0x98fb98}, {"darkorchid", 0x9932cc}, {"yellowgreen", 0x9acd32},
    {"sienna", 0xa0522d}, {"brown", 0xa52a2a}, {"darkgray", 0xa9a9a9},
    {"darkgrey", 0xa9a9a9}, {"lightblue", 0xadd8e6}, {"greenyellow", 0xadff2f},
    {"paleturquoise", 0xafeeee}, {"lightsteelblue", 0xb0c4de}, {"powderblue", 0xb0e0e6},
    {"firebrick", 0xb22222}, {"darkgoldenrod", 0xb8860b}, {"mediumorchid", 0xba55d3},
    {"rosybrown", 0xbc8f8f}, {"darkkhaki", 0xbdb76b}, {"silver", 0xc0c0c0},
    {"mediumvioletred", 0xc71585}, {"indianred", 0xcd5c5c}, {"peru", 0xcd853f},
    {"chocolate", 0xd2691e}, {"tan", 0xd2b48c}, {"lightgray", 0xd3d3d3},
    {"lightgrey", 0xd3d3d3}, {"thistle", 0xd8bfd8}, {"orchid", 0xda70d6},
    {"goldenrod", 0xdaa520}, {"palevioletred", 0xdb7093}, {"crimson", 0xdc143c},
    {"gainsboro", 0xdcdcdc}, {"plum", 0xdda0dd}, {"burlywood", 0xdeb887},
    {"lightcyan", 0xe0ffff}, {"lavender", 0xe6e6fa}, {"darksalmon", 0xe9967a},
    {"violet", 0xee82ee}, {"palegoldenrod", 0xeee8aa}, {"lightcoral", 0xf08080},
    {"khaki", 0xf0e68c}, {"aliceblue", 0xf0f8ff}, {"honeydew", 0xf0fff0},
    {"azure", 0xf0ffff}, {"sandybrown", 0xf4a460}, {"wheat", 0xf5deb3},
    {"beige", 0xf5f5dc}, {"whitesmoke", 0xf5f5f5}, {"mintcream", 0xf5fffa},
    {"ghostwhite", 0xf8f8ff}, {"salmon", 0xfa8072}, {"antiquewhite", 0xfaebd7},
    {"linen", 0xfaf0e6}, {"lightgoldenrodyellow", 0xfafad2}, {"oldlace", 0xfdf5e6},
    {"red", 0xff0000}, {"fuchsia", 0xff00ff}, {"magenta", 0xff00ff},
    {"deeppink", 0xff1493}, {"orangered", 0xff4500}, {"tomato", 0xff6347},
    {"hotpink", 0xff69b4}, {"coral", 0xff7f50}, {"darkorange", 0xff8c00},
    {"lightsalmon", 0xffa07a}, {"orange", 0xffa500}, {"lightpink", 0xffb6c1},
    {"pink", 0xffc0cb}, {"gold", 0xffd700}, {"peachpuff", 0xffdab9},
    {"navajowhite", 0xffdead}, {"moccasin", 0xffe4b5}, {"bisque", 0xffe4c4},
    {"mistyrose", 0xffe4e1}, {"blanchedalmond", 0xffebcd}, {"papayawhip", 0xffefd5},
    {"lavenderblush", 0xfff0f5}, {"seashell", 0xfff5ee}, {"cornsilk", 0xfff8dc},
    {"lemonchiffon", 0xfffacd}, {"floralwhite", 0xfffaf0}, {"snow", 0xfffafa},
    {"yellow", 0xffff00}, {"lightyellow", 0xffffe0}, {"ivory", 0xfffff0},
    {"white", 0xffffff},
};
#define NAMED_COLORS_COUNT ((int)(sizeof named_colors / sizeof named_colors[0]))
// clang-format on

/* Returns TRUE and sets *out_rgb on success.
   Flashes "Invalid hex color." and returns FALSE if val starts with '#' but is
   malformed. Returns FALSE without flashing if val is neither a valid hex nor a
   known name. */
static void palette_to_rgb(int idx, int *r, int *g, int *b) {
  if (idx < 0 || idx >= PALETTE_SIZE) { *r = *g = *b = 0; return; }
  *r = (int)(palette[idx][0] * 255 + 0.5);
  *g = (int)(palette[idx][1] * 255 + 0.5);
  *b = (int)(palette[idx][2] * 255 + 0.5);
}

static void set_palette_rgb(int slot, unsigned int rgb) {
  palette[slot][0] = ((rgb >> 16) & 0xff) / 255.0;
  palette[slot][1] = ((rgb >> 8) & 0xff) / 255.0;
  palette[slot][2] = (rgb & 0xff) / 255.0;
}

static void insert_paint(void) {
  if (insert_mode) {
    begin_undo_action();
    paint_brush(cursor_x, cursor_y, fg_color);
    commit_undo_action();
  }
}

static void draw_line(int x0, int y0, int x1, int y1, guint32 color) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  int cx = x0, cy = y0;
  while (1) {
    paint_pixel(cx, cy, color);
    if (cx == x1 && cy == y1)
      break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      cx += sx;
    }
    if (e2 < dx) {
      err += dx;
      cy += sy;
    }
  }
}

static void rgb_to_hsl(double r, double g, double b, double *h, double *s,
                       double *l) {
  double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  *l = (mx + mn) / 2.0;
  if (mx == mn) {
    *h = *s = 0.0;
    return;
  }
  double d = mx - mn;
  *s = *l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
  if (mx == r)
    *h = (g - b) / d + (g < b ? 6.0 : 0.0);
  else if (mx == g)
    *h = (b - r) / d + 2.0;
  else
    *h = (r - g) / d + 4.0;
  *h *= 60.0;
}

static double hue_to_rgb(double p, double q, double t) {
  if (t < 0)
    t += 1;
  if (t > 1)
    t -= 1;
  if (t < 1.0 / 6)
    return p + (q - p) * 6 * t;
  if (t < 0.5)
    return q;
  if (t < 2.0 / 3)
    return p + (q - p) * (2.0 / 3 - t) * 6;
  return p;
}

static void hsl_to_rgb(double h, double s, double l, double *r, double *g,
                       double *b) {
  if (s == 0.0) {
    *r = *g = *b = l;
    return;
  }
  h /= 360.0;
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  double p = 2 * l - q;
  *r = hue_to_rgb(p, q, h + 1.0 / 3);
  *g = hue_to_rgb(p, q, h);
  *b = hue_to_rgb(p, q, h - 1.0 / 3);
}

static gboolean parse_color(const char *val, unsigned int *out_rgb) {
  if (val[0] == '#') {
    size_t len = strlen(val);
    if (len == 4) {
      unsigned int r, g, b;
      if (sscanf(val + 1, "%1x%1x%1x", &r, &g, &b) == 3) {
        *out_rgb = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
        return TRUE;
      }
    } else if (len >= 7) {
      char tmp[8];
      strncpy(tmp, val + 1, 6);
      tmp[6] = '\0';
      if (sscanf(tmp, "%06x", out_rgb) == 1)
        return TRUE;
    }
    cmd_flash("Invalid hex color.");
    return FALSE;
  }
  for (int i = 0; i < NAMED_COLORS_COUNT; i++) {
    if (strcasecmp(val, named_colors[i].name) == 0) {
      *out_rgb = named_colors[i].rgb;
      return TRUE;
    }
  }
  return FALSE;
}

static void cmd_open(const char *filename) {
  cairo_surface_t *surf = cairo_image_surface_create_from_png(filename);
  if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
    cmd_flash("Open failed.");
    cairo_surface_destroy(surf);
    return;
  }
  int iw = cairo_image_surface_get_width(surf);
  int ih = cairo_image_surface_get_height(surf);
  if (iw > CANVAS_W || ih > CANVAS_H) {
    int nw = MAX(iw, CANVAS_W), nh = MAX(ih, CANVAS_H);
    guint32 *np = calloc((size_t)nw * nh, sizeof(guint32));
    if (!np) {
      cmd_flash("Out of memory.");
      cairo_surface_destroy(surf);
      return;
    }
    free(pixels);
    pixels = np;
    CANVAS_W = nw;
    CANVAS_H = nh;
    cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
    cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
    visual_anchor_x = CLAMP(visual_anchor_x, 0, CANVAS_W - 1);
    visual_anchor_y = CLAMP(visual_anchor_y, 0, CANVAS_H - 1);
    zoom_resize();
  }
  guchar *d = cairo_image_surface_get_data(surf);
  int st = cairo_image_surface_get_stride(surf);
  memset(pixels, 0, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  for (int y = 0; y < ih; y++)
    for (int x = 0; x < iw; x++) {
      /* Cairo ARGB32 on LE: bytes are B, G, R, A */
      guchar b = d[y * st + x * 4 + 0];
      guchar g = d[y * st + x * 4 + 1];
      guchar r = d[y * st + x * 4 + 2];
      guchar a = d[y * st + x * 4 + 3];
      PX(y, x) = PACK_RGBA(r, g, b, a);
    }
  cairo_surface_destroy(surf);
  layers_flatten();
  layer_bufs[0] = pixels;
  clear_history();
  canvas_dirty = FALSE;
  snprintf(last_filename, sizeof(last_filename), "%s", filename);
  update_title(last_filename);
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

static void cmd_execute(void) {
  /* Extract argument: text after the command verb, leading spaces stripped */
  const char *p = cmd_buf + 1;
  while (*p && *p != ' ')
    p++;
  while (*p == ' ')
    p++;
  const char *arg = p;

  if (strcmp(cmd_buf, ":q") == 0) {
    if (canvas_dirty)
      cmd_flash(
          "Unsaved changes. Use :q! to force quit or :wq to save and quit.");
    else if (tab_count > 1)
      tab_close_current();
    else
      gtk_main_quit();
    return;
  }

  if (strcmp(cmd_buf, ":q!") == 0) {
    if (tab_count > 1)
      tab_close_current();
    else
      gtk_main_quit();
    return;
  }

  if (strncmp(cmd_buf, ":tabnew", 7) == 0) {
    if (tab_count >= TAB_MAX) {
      cmd_flash("Max tabs reached.");
      return;
    }
    tab_save(tab_current);
    guint32 *np = calloc(DEFAULT_CANVAS_W * DEFAULT_CANVAS_H, sizeof(guint32));
    if (!np) {
      cmd_flash("Out of memory.");
      return;
    }
    free(pixels);
    pixels = np;
    CANVAS_W = DEFAULT_CANVAS_W;
    CANVAS_H = DEFAULT_CANVAS_H;
    cursor_x = cursor_y = 0;
    canvas_dirty = FALSE;
    visual_mode = FALSE;
    insert_mode = FALSE;
    last_filename[0] = '\0';
    clear_history();
    tab_count++;
    tab_current = tab_count - 1;
    /* Also save the initial empty state into tabs[tab_current] */
    tab_save(tab_current);
    if (*arg)
      cmd_open(arg);
    zoom_resize();
    title_refresh();
    status_update();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strcmp(cmd_buf, ":new") == 0) {
    if (canvas_dirty) {
      cmd_flash("Unsaved changes. Use :new! to discard or :w to save first.");
      return;
    }
    guint32 *np = calloc(DEFAULT_CANVAS_W * DEFAULT_CANVAS_H, sizeof(guint32));
    if (!np) {
      cmd_flash("Out of memory.");
      return;
    }
    free(pixels);
    pixels = np;
    CANVAS_W = DEFAULT_CANVAS_W;
    CANVAS_H = DEFAULT_CANVAS_H;
    clear_history();
    canvas_dirty = FALSE;
    last_filename[0] = '\0';
    cursor_x = 0;
    cursor_y = 0;
    gtk_window_set_title(GTK_WINDOW(main_window), "vim-paint");
    zoom_resize();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strcmp(cmd_buf, ":new!") == 0) {
    guint32 *np = calloc(DEFAULT_CANVAS_W * DEFAULT_CANVAS_H, sizeof(guint32));
    if (!np) {
      cmd_flash("Out of memory.");
      return;
    }
    free(pixels);
    pixels = np;
    CANVAS_W = DEFAULT_CANVAS_W;
    CANVAS_H = DEFAULT_CANVAS_H;
    clear_history();
    canvas_dirty = FALSE;
    last_filename[0] = '\0';
    cursor_x = 0;
    cursor_y = 0;
    gtk_window_set_title(GTK_WINDOW(main_window), "vim-paint");
    zoom_resize();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strcmp(cmd_buf, ":w") == 0) {
    if (*last_filename)
      cmd_write(last_filename);
    else
      cmd_flash("No filename. Use :w filename");
    return;
  }

  if (strcmp(cmd_buf, ":wq") == 0) {
    if (*last_filename) {
      if (cmd_write(last_filename))
        gtk_main_quit();
    } else {
      cmd_flash("No filename. Use :wq filename");
    }
    return;
  }

  if (strncmp(cmd_buf, ":w ", 3) == 0 && *arg) {
    if (cmd_write(arg)) {
      snprintf(last_filename, sizeof(last_filename), "%s", arg);
      update_title(last_filename);
    }
    return;
  }

  if (strncmp(cmd_buf, ":wq ", 4) == 0 && *arg) {
    if (cmd_write(arg)) {
      snprintf(last_filename, sizeof(last_filename), "%s", arg);
      update_title(last_filename);
      gtk_main_quit();
    }
    return;
  }

  if (strcmp(cmd_buf, ":e") == 0) {
    if (canvas_dirty) {
      cmd_flash("Unsaved changes. Use :e! to discard or :w to save first.");
      return;
    }
    if (*last_filename)
      cmd_open(last_filename);
    else
      cmd_flash("No filename.");
    return;
  }

  if (strcmp(cmd_buf, ":e!") == 0) {
    if (*last_filename)
      cmd_open(last_filename);
    else
      cmd_flash("No filename.");
    return;
  }

  if (strncmp(cmd_buf, ":e ", 3) == 0 && *arg) {
    if (canvas_dirty) {
      cmd_flash("Unsaved changes. Use :e! to discard or :w to save first.");
      return;
    }
    cmd_open(arg);
    return;
  }

  if (strncmp(cmd_buf, ":e! ", 4) == 0 && *arg) {
    cmd_open(arg);
    return;
  }

  if (strncmp(cmd_buf, ":set ", 5) == 0) {
    const char *opt = cmd_buf + 5;
    if (strcmp(opt, "grid") == 0) {
      show_grid = TRUE;
      gtk_widget_queue_draw(main_canvas);
      cmd_set("");
    } else if (strcmp(opt, "nogrid") == 0) {
      show_grid = FALSE;
      gtk_widget_queue_draw(main_canvas);
      cmd_set("");
    } else if (strncmp(opt, "gridcolor ", 10) == 0) {
      unsigned int rgb = 0;
      if (parse_color(opt + 10, &rgb)) {
        grid_color = PACK_RGBA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
      }
    } else if (strcmp(opt, "ruler") == 0) {
      show_ruler = TRUE;
      gtk_widget_queue_draw(main_canvas);
      cmd_set("");
    } else if (strcmp(opt, "noruler") == 0) {
      show_ruler = FALSE;
      gtk_widget_queue_draw(main_canvas);
      cmd_set("");
    } else if (strcmp(opt, "checker") == 0) {
      show_checker = TRUE;
      gtk_widget_queue_draw(main_canvas);
      cmd_set("");
    } else if (strcmp(opt, "nochecker") == 0) {
      show_checker = FALSE;
      gtk_widget_queue_draw(main_canvas);
      cmd_set("");
    } else if (strncmp(opt, "zoom ", 5) == 0) {
      int v = atoi(opt + 5);
      if (v >= 4 && v <= 64) {
        CELL_SIZE = v;
        zoom_resize();
        status_update();
      } else {
        cmd_flash("Zoom must be 4-64.");
      }
    } else if (strncmp(opt, "undolevels ", 11) == 0) {
      int v = atoi(opt + 11);
      if (v >= 1 && v <= UNDO_MAX) {
        clear_history();
        undo_levels = v;
        cmd_flash("Undo levels set (history cleared).");
      } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "Undo levels must be 1-%d.", UNDO_MAX);
        cmd_flash(msg);
      }
    } else if (strncmp(opt, "brush ", 6) == 0) {
      int v = atoi(opt + 6);
      if (v >= 1 && v <= 16) {
        brush_size = v;
        cmd_set("");
      } else {
        cmd_flash("Brush size must be 1-16.");
      }
    } else if (strncmp(opt, "brushshape ", 11) == 0) {
      const char *shape = opt + 11;
      if (strcmp(shape, "circle") == 0) {
        brush_shape = 1;
        cmd_set("");
      } else if (strcmp(shape, "square") == 0) {
        brush_shape = 0;
        cmd_set("");
      } else if (strcmp(shape, "custom") == 0) {
        if (custom_brush_w == 0) {
          cmd_flash("No custom brush defined. Use :brushdefine first.");
        } else {
          brush_shape = 2;
          cmd_set("");
        }
      } else {
        cmd_flash("Usage: :set brushshape square|circle|custom");
      }
    } else if (strncmp(opt, "spray ", 6) == 0) {
      const char *val = opt + 6;
      if (strcmp(val, "off") == 0) {
        spray_density = 0;
        cmd_set("");
      } else {
        int v = atoi(val);
        if (v >= 1 && v <= 100) {
          spray_density = v;
          cmd_set("");
        } else {
          cmd_flash("Usage: :set spray <1-100>|off");
        }
      }
    } else if (strncmp(opt, "fill ", 5) == 0) {
      const char *val = opt + 5;
      if (strcmp(val, "solid") == 0) {
        fill_pattern = FILL_SOLID; cmd_flash("Fill: solid");
      } else if (strcmp(val, "checker") == 0) {
        fill_pattern = FILL_CHECKER; cmd_flash("Fill: checkerboard");
      } else if (strcmp(val, "hstripes") == 0) {
        fill_pattern = FILL_HSTRIPES; cmd_flash("Fill: horizontal stripes");
      } else if (strcmp(val, "vstripes") == 0) {
        fill_pattern = FILL_VSTRIPES; cmd_flash("Fill: vertical stripes");
      } else {
        cmd_flash("Usage: :set fill solid|checker|hstripes|vstripes|halftone");
      }
    } else if (strncmp(opt, "bg ", 3) == 0) {
      const char *val = opt + 3;
      unsigned int rgb = 0;
      if (parse_color(val, &rgb)) {
        bg_color =
            PACK_RGBA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
      } else if (val[0] != '#') {
        cmd_flash("Unknown color.");
      }
    } else if (strncmp(opt, "color ", 6) == 0) {
      const char *val = opt + 6;
      /* two-arg form: :set color <idx> <color> - edit slot in place */
      const char *sp = strchr(val, ' ');
      if (sp && val[0] >= '0' && val[0] <= '9') {
        int slot = atoi(val);
        const char *cval = sp + 1;
        while (*cval == ' ')
          cval++;
        if (slot < 0 || slot >= PALETTE_SIZE) {
          cmd_flash("Invalid palette index.");
        } else {
          unsigned int rgb2 = 0;
          if (parse_color(cval, &rgb2)) {
            set_palette_rgb(slot, rgb2);
            gtk_widget_queue_draw(palette_bar);
            gtk_widget_queue_draw(main_canvas);
            if (slot == 0)
              cmd_flash("Background color updated.");
            else
              cmd_set("");
          } else if (cval[0] != '#') {
            cmd_flash("Unknown color.");
          }
        }
        return;
      }
      unsigned int rgb = 0;
      if (!parse_color(val, &rgb)) {
        if (val[0] != '#') {
          int n = atoi(val);
          if (n >= 0 && n < PALETTE_SIZE) {
            int pr2, pg2, pb2;
            palette_to_rgb(n, &pr2, &pg2, &pb2);
            fg_color = PACK_RGBA(pr2, pg2, pb2, 255);
            gtk_widget_queue_draw(palette_bar);
            flash_color(n);
          } else {
            cmd_flash("Unknown color.");
          }
        }
      } else {
        double pr = ((rgb >> 16) & 0xff) / 255.0;
        double pg = ((rgb >> 8) & 0xff) / 255.0;
        double pb = (rgb & 0xff) / 255.0;
        int found = -1;
        for (int i = 0; i < PALETTE_SIZE; i++) {
          if (palette[i][0] == pr && palette[i][1] == pg &&
              palette[i][2] == pb) {
            found = i;
            break;
          }
        }
        if (found < 0) {
          if (PALETTE_SIZE >= 256) {
            cmd_flash("Palette full.");
          } else if (!palette_reserve(palette_size + 1)) {
            cmd_flash("Out of memory.");
          } else {
            found = palette_size;
            set_palette_rgb(found, rgb);
            palette_size++;
            fg_color = PACK_RGBA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff,
                                 rgb & 0xff, 255);
            gtk_widget_queue_draw(palette_bar);
            flash_color(found);
          }
        } else {
          fg_color =
              PACK_RGBA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
          gtk_widget_queue_draw(palette_bar);
          flash_color(found);
        }
      }
    } else if (strcmp(opt, "sym h") == 0) {
      sym_mode = SYM_H;
      cmd_flash("Symmetry: horizontal");
    } else if (strcmp(opt, "sym v") == 0) {
      sym_mode = SYM_V;
      cmd_flash("Symmetry: vertical");
    } else if (strcmp(opt, "sym hv") == 0 || strcmp(opt, "sym vh") == 0 ||
               strcmp(opt, "sym 4") == 0) {
      sym_mode = SYM_HV;
      cmd_flash("Symmetry: 4-way");
    } else if (strncmp(opt, "sym radial", 10) == 0) {
      int n = atoi(opt + 10);
      if (n < 2 || n > 32) n = 4;
      sym_radial_n = n;
      sym_mode = SYM_RADIAL;
      char msg[48];
      snprintf(msg, sizeof(msg), "Symmetry: %d-point radial", sym_radial_n);
      cmd_flash(msg);
    } else if (strcmp(opt, "sym none") == 0 || strcmp(opt, "nosym") == 0) {
      sym_mode = SYM_NONE;
      cmd_flash("Symmetry: off");
    } else if (strncmp(opt, "font ", 5) == 0) {
      /* :set font <family> <size>  or  :set font <family> */
      const char *farg = opt + 5;
      /* Try to find a trailing number for size */
      const char *last_sp = strrchr(farg, ' ');
      if (last_sp && last_sp[1] != '\0') {
        char *end;
        double sz = strtod(last_sp + 1, &end);
        if (*end == '\0' && sz >= 1.0 && sz <= 256.0) {
          size_t flen = (size_t)(last_sp - farg);
          if (flen > 0 && flen < sizeof(text_font_family)) {
            memcpy(text_font_family, farg, flen);
            text_font_family[flen] = '\0';
          }
          text_font_size = sz;
          char msg[320];
          snprintf(msg, sizeof(msg), "Font: %s %.4gpt", text_font_family,
                   text_font_size);
          cmd_flash(msg);
          return;
        }
      }
      /* No size given - just update family */
      if (strlen(farg) > 0 && strlen(farg) < sizeof(text_font_family)) {
        snprintf(text_font_family, sizeof(text_font_family), "%s", farg);
        char msg[320];
        snprintf(msg, sizeof(msg), "Font: %s %.4gpt", text_font_family,
                 text_font_size);
        cmd_flash(msg);
      } else {
        cmd_flash("Invalid font family.");
      }
    } else {
      cmd_flash("Unknown option.");
    }
    return;
  }

  if (strncmp(cmd_buf, ":replace ", 9) == 0 && *arg) {
    char from_s[64] = "", to_s[64] = "";
    if (sscanf(arg, "%63s %63s", from_s, to_s) != 2) {
      cmd_flash("Usage: :replace <from> <to>");
      return;
    }
    unsigned int from_rgb, to_rgb;
    if (!parse_color(from_s, &from_rgb) || !parse_color(to_s, &to_rgb))
      return;
    guint32 from_px = PACK_RGBA((from_rgb >> 16) & 0xff, (from_rgb >> 8) & 0xff,
                                from_rgb & 0xff, 255);
    guint32 to_px = PACK_RGBA((to_rgb >> 16) & 0xff, (to_rgb >> 8) & 0xff,
                              to_rgb & 0xff, 255);
    if (from_px == to_px) {
      cmd_flash("Colors are the same.");
      return;
    }
    begin_undo_action();
    for (int y = 0; y < CANVAS_H; y++)
      for (int x = 0; x < CANVAS_W; x++)
        if (PX(y, x) == from_px) {
          push_undo(x, y);
          PX(y, x) = to_px;
        }
    commit_undo_action();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":gradient ", 10) == 0 && *arg) {
    char c1s[64] = "", c2s[64] = "", dir[4] = "";
    if (sscanf(arg, "%63s %63s %3s", c1s, c2s, dir) != 3 ||
        (strcmp(dir, "h") != 0 && strcmp(dir, "v") != 0)) {
      cmd_flash("Usage: :gradient <color1> <color2> h|v");
      return;
    }
    unsigned int rgb1, rgb2;
    if (!parse_color(c1s, &rgb1) || !parse_color(c2s, &rgb2)) {
      cmd_flash("Unknown color.");
      return;
    }
    int horiz = (dir[0] == 'h');
    int x0 = 0, y0 = 0, x1 = CANVAS_W - 1, y1 = CANVAS_H - 1;
    if (visual_mode) {
      x0 = MIN(cursor_x, visual_anchor_x);
      x1 = MAX(cursor_x, visual_anchor_x);
      y0 = MIN(cursor_y, visual_anchor_y);
      y1 = MAX(cursor_y, visual_anchor_y);
    }
    int span = horiz ? (x1 - x0 + 1) : (y1 - y0 + 1);
    int r1i = (rgb1 >> 16) & 0xff, g1i = (rgb1 >> 8) & 0xff, b1i = rgb1 & 0xff;
    int r2i = (rgb2 >> 16) & 0xff, g2i = (rgb2 >> 8) & 0xff, b2i = rgb2 & 0xff;
    guint32 *pos_color = malloc(span * sizeof(guint32));
    if (!pos_color) {
      cmd_flash("Out of memory.");
      return;
    }
    for (int i = 0; i < span; i++) {
      double t = span > 1 ? i / (double)(span - 1) : 0.0;
      int ri = (int)(r1i + t * (r2i - r1i) + 0.5);
      int gi = (int)(g1i + t * (g2i - g1i) + 0.5);
      int bi = (int)(b1i + t * (b2i - b1i) + 0.5);
      pos_color[i] = PACK_RGBA(ri, gi, bi, 255);
    }
    begin_undo_action();
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        push_undo(x, y);
        PX(y, x) = pos_color[horiz ? (x - x0) : (y - y0)];
      }
    commit_undo_action();
    free(pos_color);
    visual_mode = FALSE;
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":gradtool", 9) == 0) {
    /* :gradtool [c1 c2]  — enter interactive gradient drag mode.
       Colors default to current fg/bg if not given. */
    unsigned int rgb1 = 0, rgb2 = 0;
    if (*arg) {
      char c1s[64] = "", c2s[64] = "";
      if (sscanf(arg, "%63s %63s", c1s, c2s) != 2 ||
          !parse_color(c1s, &rgb1) || !parse_color(c2s, &rgb2)) {
        cmd_flash("Usage: :gradtool [color1 color2]");
        return;
      }
    } else {
      /* Extract 24-bit RGB from packed fg/bg */
      rgb1 = ((fg_color >> 8) & 0xffffff);
      rgb2 = ((bg_color >> 8) & 0xffffff);
    }
    grad_c1 = rgb1;
    grad_c2 = rgb2;
    gradient_tool = TRUE;
    grad_dragging = FALSE;
    cmd_flash("Click and drag to apply gradient (Esc to cancel)");
    return;
  }

  if (strcmp(cmd_buf, ":brushdefine") == 0 ||
      strncmp(cmd_buf, ":brushdefine ", 13) == 0) {
    if (*arg) {
      /* Pattern string: rows separated by '/', '#'/'*'/'1' = set, rest = clear */
      memset(custom_brush_pixels, 0, sizeof(custom_brush_pixels));
      int row = 0, maxcol = 0;
      const char *p = arg;
      while (*p && row < CUSTOM_BRUSH_MAX) {
        int col = 0;
        while (*p && *p != '/' && col < CUSTOM_BRUSH_MAX) {
          custom_brush_pixels[row][col] =
              (*p == '#' || *p == '*' || *p == '1') ? TRUE : FALSE;
          col++;
          p++;
        }
        if (col > maxcol) maxcol = col;
        row++;
        if (*p == '/') p++;
      }
      if (row == 0 || maxcol == 0) {
        cmd_flash("Empty pattern.");
        return;
      }
      custom_brush_w = maxcol;
      custom_brush_h = row;
    } else if (visual_mode) {
      /* Capture shape from visual selection: non-transparent pixels = set */
      int x0 = MIN(cursor_x, visual_anchor_x);
      int x1 = MAX(cursor_x, visual_anchor_x);
      int y0 = MIN(cursor_y, visual_anchor_y);
      int y1 = MAX(cursor_y, visual_anchor_y);
      int w = x1 - x0 + 1, h = y1 - y0 + 1;
      if (w > CUSTOM_BRUSH_MAX) w = CUSTOM_BRUSH_MAX;
      if (h > CUSTOM_BRUSH_MAX) h = CUSTOM_BRUSH_MAX;
      memset(custom_brush_pixels, 0, sizeof(custom_brush_pixels));
      for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
          custom_brush_pixels[dy][dx] = (PX(y0 + dy, x0 + dx) & 0xff) != 0;
      custom_brush_w = w;
      custom_brush_h = h;
      visual_mode = FALSE;
    } else {
      cmd_flash("Usage: :brushdefine <pattern>  or select region first");
      return;
    }
    brush_shape = 2;
    char msg[64];
    snprintf(msg, sizeof(msg), "Custom brush defined (%dx%d).",
             custom_brush_w, custom_brush_h);
    cmd_flash(msg);
    return;
  }

  if (strncmp(cmd_buf, ":hue ", 5) == 0 || strncmp(cmd_buf, ":sat ", 5) == 0 ||
      strncmp(cmd_buf, ":bright ", 8) == 0) {
    double delta = atof(arg);
    int is_hue = (cmd_buf[1] == 'h');
    int is_sat = (cmd_buf[1] == 's');
    int x0 = 0, y0 = 0, x1 = CANVAS_W - 1, y1 = CANVAS_H - 1;
    if (visual_mode) {
      x0 = MIN(cursor_x, visual_anchor_x);
      x1 = MAX(cursor_x, visual_anchor_x);
      y0 = MIN(cursor_y, visual_anchor_y);
      y1 = MAX(cursor_y, visual_anchor_y);
    }
    begin_undo_action();
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        guint32 px = PX(y, x);
        guchar a = px & 0xff;
        if (a == 0)
          continue;
        double r = ((px >> 24) & 0xff) / 255.0;
        double g = ((px >> 16) & 0xff) / 255.0;
        double b = ((px >> 8) & 0xff) / 255.0;
        double h, s, l;
        rgb_to_hsl(r, g, b, &h, &s, &l);
        if (is_hue)
          h = fmod(h + delta + 720.0, 360.0);
        else if (is_sat)
          s = CLAMP(s + delta / 100.0, 0.0, 1.0);
        else
          l = CLAMP(l + delta / 100.0, 0.0, 1.0);
        hsl_to_rgb(h, s, l, &r, &g, &b);
        push_undo(x, y);
        PX(y, x) = PACK_RGBA((int)(r * 255 + 0.5), (int)(g * 255 + 0.5),
                             (int)(b * 255 + 0.5), a);
      }
    commit_undo_action();
    visual_mode = FALSE;
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":dither ", 8) == 0 && *arg) {
    char c1s[64] = "", c2s[64] = "", pat[16] = "";
    if (sscanf(arg, "%63s %63s %15s", c1s, c2s, pat) != 3 ||
        (strcmp(pat, "ordered") != 0 && strcmp(pat, "fs") != 0)) {
      cmd_flash("Usage: :dither <color1> <color2> ordered|fs");
      return;
    }
    unsigned int rgb1, rgb2;
    if (!parse_color(c1s, &rgb1) || !parse_color(c2s, &rgb2)) {
      cmd_flash("Unknown color.");
      return;
    }
    int r1 = (rgb1 >> 16) & 0xff, g1 = (rgb1 >> 8) & 0xff, b1 = rgb1 & 0xff;
    int r2 = (rgb2 >> 16) & 0xff, g2 = (rgb2 >> 8) & 0xff, b2 = rgb2 & 0xff;
    guint32 px1 = PACK_RGBA(r1, g1, b1, 255);
    guint32 px2 = PACK_RGBA(r2, g2, b2, 255);
    int x0 = 0, y0 = 0, x1 = CANVAS_W - 1, y1 = CANVAS_H - 1;
    if (visual_mode) {
      x0 = MIN(cursor_x, visual_anchor_x);
      x1 = MAX(cursor_x, visual_anchor_x);
      y0 = MIN(cursor_y, visual_anchor_y);
      y1 = MAX(cursor_y, visual_anchor_y);
    }
    guint32 *before_snap = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    if (!before_snap) {
      cmd_flash("Out of memory.");
      return;
    }
    memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));

    if (strcmp(pat, "ordered") == 0) {
      static const int bayer[4][4] = {
          {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
          guint32 px = PX(y, x);
          if ((px & 0xff) == 0)
            continue;
          int pr = (px >> 24) & 0xff, pg = (px >> 16) & 0xff,
              pb = (px >> 8) & 0xff;
          int dr1 = pr - r1, dg1 = pg - g1, db1 = pb - b1;
          int dr2 = pr - r2, dg2 = pg - g2, db2 = pb - b2;
          double d1 = dr1 * dr1 + dg1 * dg1 + db1 * db1;
          double d2 = dr2 * dr2 + dg2 * dg2 + db2 * db2;
          double t = (d1 + d2 > 0) ? d1 / (d1 + d2) : 0.5;
          PX(y, x) = (t >= (bayer[y % 4][x % 4] + 0.5) / 16.0) ? px2 : px1;
        }
    } else {
      int W = x1 - x0 + 1, H = y1 - y0 + 1;
      double *er = calloc((size_t)W * H, sizeof(double));
      double *eg = calloc((size_t)W * H, sizeof(double));
      double *eb = calloc((size_t)W * H, sizeof(double));
      if (!er || !eg || !eb) {
        free(er);
        free(eg);
        free(eb);
        free(before_snap);
        cmd_flash("Out of memory.");
        return;
      }
      for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
          guint32 px = PX(y, x);
          if ((px & 0xff) == 0)
            continue;
          int ey = y - y0, ex = x - x0, idx = ey * W + ex;
          double nr = ((px >> 24) & 0xff) + er[idx];
          double ng = ((px >> 16) & 0xff) + eg[idx];
          double nb = ((px >> 8) & 0xff) + eb[idx];
          int dr1 = (int)nr - r1, dg1 = (int)ng - g1, db1 = (int)nb - b1;
          int dr2 = (int)nr - r2, dg2 = (int)ng - g2, db2 = (int)nb - b2;
          guint32 chosen;
          int cr, cg, cb;
          if (dr1 * dr1 + dg1 * dg1 + db1 * db1 <=
              dr2 * dr2 + dg2 * dg2 + db2 * db2) {
            chosen = px1;
            cr = r1;
            cg = g1;
            cb = b1;
          } else {
            chosen = px2;
            cr = r2;
            cg = g2;
            cb = b2;
          }
          PX(y, x) = chosen;
          double qr = nr - cr, qg = ng - cg, qb = nb - cb;
#define SPREAD(dy, dx, w)                                                      \
  do {                                                                         \
    int ny = ey + (dy), nx = ex + (dx);                                        \
    if (ny >= 0 && ny < H && nx >= 0 && nx < W) {                              \
      int ni = ny * W + nx;                                                    \
      er[ni] += qr * (w);                                                      \
      eg[ni] += qg * (w);                                                      \
      eb[ni] += qb * (w);                                                      \
    }                                                                          \
  } while (0)
          SPREAD(0, 1, 7.0 / 16);
          SPREAD(1, -1, 3.0 / 16);
          SPREAD(1, 0, 5.0 / 16);
          SPREAD(1, 1, 1.0 / 16);
#undef SPREAD
        }
      }
      free(er);
      free(eg);
      free(eb);
    }
    commit_canvas_snapshot(before_snap, CANVAS_W, CANVAS_H);
    visual_mode = FALSE;
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":find color ", 12) == 0) {
    const char *val = cmd_buf + 12;
    unsigned int rgb;
    if (!parse_color(val, &rgb)) {
      cmd_flash("Unknown color.");
      return;
    }
    int tr = (rgb >> 16) & 0xff;
    int tg = (rgb >> 8) & 0xff;
    int tb = rgb & 0xff;
    /* Find the nearest pixel (Manhattan) whose RGB is closest to the requested
     * color */
    int found_x = -1, found_y = -1, found_dist = INT_MAX;
    double found_color_dist = 1e9;
    for (int y = 0; y < CANVAS_H; y++) {
      for (int x = 0; x < CANVAS_W; x++) {
        guint32 px = PX(y, x);
        int pr = (px >> 24) & 0xff;
        int pg = (px >> 16) & 0xff;
        int pb = (px >> 8) & 0xff;
        double dr = (pr - tr) / 255.0, dg = (pg - tg) / 255.0,
               db = (pb - tb) / 255.0;
        double cdist = dr * dr + dg * dg + db * db;
        int mdist = abs(x - cursor_x) + abs(y - cursor_y);
        if (cdist < found_color_dist ||
            (cdist == found_color_dist && mdist < found_dist)) {
          found_color_dist = cdist;
          found_dist = mdist;
          found_x = x;
          found_y = y;
        }
      }
    }
    if (found_x < 0) {
      cmd_flash("Color not found on canvas.");
    } else {
      cursor_x = found_x;
      cursor_y = found_y;
      status_update();
      gtk_widget_queue_draw(main_canvas);
    }
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":goto ", 6) == 0 && *arg) {
    int gx = 0, gy = 0;
    if (sscanf(arg, "%d,%d", &gx, &gy) != 2)
      sscanf(arg, "%d %d", &gx, &gy);
    if (gx < 1 || gy < 1) {
      cmd_flash("Usage: :goto col,row  (1-based)");
      return;
    }
    cursor_x = CLAMP(gx - 1, 0, CANVAS_W - 1);
    cursor_y = CLAMP(gy - 1, 0, CANVAS_H - 1);
    status_update();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":scale ", 7) == 0 && *arg) {
    int n = atoi(arg);
    if (n < 2 || n > 8) {
      cmd_flash("Usage: :scale N  (N = 2..8, requires visual selection)");
      return;
    }
    if (!visual_mode) {
      cmd_flash(":scale requires a visual selection.");
      return;
    }
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    int W = x1 - x0 + 1, H = y1 - y0 + 1;
    guint32 *tmp = malloc((size_t)W * H * sizeof(guint32));
    if (!tmp) {
      cmd_flash("Out of memory.");
      return;
    }
    for (int sy = 0; sy < H; sy++)
      for (int sx = 0; sx < W; sx++)
        tmp[sy * W + sx] = PX(y0 + sy, x0 + sx);
    begin_undo_action();
    for (int sy = 0; sy < H; sy++)
      for (int sx = 0; sx < W; sx++) {
        guint32 col = tmp[sy * W + sx];
        for (int dy = 0; dy < n; dy++)
          for (int dx = 0; dx < n; dx++) {
            int px = x0 + sx * n + dx;
            int py = y0 + sy * n + dy;
            if (px < CANVAS_W && py < CANVAS_H)
              paint_pixel(px, py, col);
          }
      }
    free(tmp);
    commit_undo_action();
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strcmp(cmd_buf, ":newlayer") == 0) {
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
    /* Insert new layer above active layer */
    int ins = layer_active + 1;
    for (int li = layer_count; li > ins; li--) {
      layer_bufs[li] = layer_bufs[li - 1];
      layer_visible[li] = layer_visible[li - 1];
      layer_blend[li] = layer_blend[li - 1];
      memcpy(layer_name[li], layer_name[li - 1], 32);
    }
    layer_bufs[ins] = buf;
    layer_visible[ins] = TRUE;
    layer_blend[ins] = BLEND_NORMAL;
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
    return;
  }

  if (strcmp(cmd_buf, ":mergedown") == 0) {
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
    /* Composite active layer over the layer below */
    for (int i = 0; i < total; i++) {
      guint32 s = src[i];
      double sa = (s & 0xff) / 255.0;
      if (sa == 0.0)
        continue;
      guint32 d = dst[i];
      double da = (d & 0xff) / 255.0;
      double ra = sa + da * (1.0 - sa);
      if (ra < 1e-6) { dst[i] = 0; continue; }
      double inv = 1.0 - sa;
      int rr = (int)(((s>>24&0xff)/255.0*sa + (d>>24&0xff)/255.0*da*inv)/ra*255+0.5);
      int rg = (int)(((s>>16&0xff)/255.0*sa + (d>>16&0xff)/255.0*da*inv)/ra*255+0.5);
      int rb = (int)(((s>> 8&0xff)/255.0*sa + (d>> 8&0xff)/255.0*da*inv)/ra*255+0.5);
      dst[i] = PACK_RGBA(CLAMP(rr,0,255),CLAMP(rg,0,255),CLAMP(rb,0,255),CLAMP((int)(ra*255+0.5),0,255));
    }
    free(layer_bufs[layer_active]);
    for (int li = layer_active; li < layer_count - 1; li++) {
      layer_bufs[li] = layer_bufs[li + 1];
      layer_visible[li] = layer_visible[li + 1];
      layer_blend[li] = layer_blend[li + 1];
      memcpy(layer_name[li], layer_name[li + 1], 32);
    }
    layer_count--;
    layer_bufs[layer_count] = NULL;
    layer_visible[layer_count] = FALSE;
    layer_blend[layer_count] = BLEND_NORMAL;
    layer_active--;
    pixels = layer_bufs[layer_active];
    commit_canvas_snapshot(before, CANVAS_W, CANVAS_H);
    status_update();
    gtk_widget_queue_draw(main_canvas);
    cmd_flash("Merged down.");
    return;
  }

  if (strncmp(cmd_buf, ":layer ", 7) == 0 && *arg) {
    int n = atoi(arg) - 1; /* 1-based input */
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
    return;
  }

  if (strncmp(cmd_buf, ":layervis ", 10) == 0 && *arg) {
    int n = atoi(arg) - 1; /* 1-based input */
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
    return;
  }

  if (strncmp(cmd_buf, ":layerblend ", 12) == 0 && *arg) {
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
        if (m) strncat(modes, "|", sizeof(modes) - strlen(modes) - 1);
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
    return;
  }

  if (strncmp(cmd_buf, ":guide ", 7) == 0 && *arg) {
    /* :guide h N  — toggle horizontal guide at row N (1-based)
       :guide v N  — toggle vertical guide at column N (1-based)
       :guide clear — remove all guides
       :guide snap  — toggle snap-to-guides */
    if (strcmp(arg, "clear") == 0) {
      guide_count = 0;
      gtk_widget_queue_draw(main_canvas);
      cmd_flash("Guides cleared.");
      return;
    }
    if (strcmp(arg, "snap") == 0) {
      guide_snap = !guide_snap;
      char msg[48];
      snprintf(msg, sizeof(msg), "Guide snap %s.", guide_snap ? "on" : "off");
      cmd_flash(msg);
      return;
    }
    char dir = 0;
    int coord = 0;
    if (sscanf(arg, "%c %d", &dir, &coord) == 2 &&
        (dir == 'h' || dir == 'v') && coord >= 1) {
      gboolean horiz = (dir == 'h');
      int c = coord - 1; /* convert to 0-based */
      int limit = horiz ? CANVAS_H : CANVAS_W;
      if (c < 0 || c >= limit) {
        cmd_flash("Guide coordinate out of range.");
        return;
      }
      /* Toggle: remove if already present */
      for (int i = 0; i < guide_count; i++) {
        if (guides[i].horizontal == horiz && guides[i].coord == c) {
          guides[i] = guides[--guide_count];
          gtk_widget_queue_draw(main_canvas);
          char tmsg[48];
          snprintf(tmsg, sizeof(tmsg), "Guide %c%d removed.", dir, coord);
          cmd_flash(tmsg);
          return;
        }
      }
      if (guide_count >= GUIDE_MAX) {
        cmd_flash("Guide limit reached.");
        return;
      }
      guides[guide_count++] = (Guide){.coord = c, .horizontal = horiz};
      gtk_widget_queue_draw(main_canvas);
      char tmsg[48];
      snprintf(tmsg, sizeof(tmsg), "Guide %c%d added.", dir, coord);
      cmd_flash(tmsg);
      return;
    }
    cmd_flash("Usage: :guide h|v N  |  :guide clear  |  :guide snap");
    return;
  }

  if (strncmp(cmd_buf, ":resize ", 8) == 0 && *arg) {
    int nw = 0, nh = 0;
    if (sscanf(arg, "%dx%d", &nw, &nh) != 2)
      sscanf(arg, "%d %d", &nw, &nh);
    if (nw < 1 || nh < 1 || nw > 16384 || nh > 16384) {
      cmd_flash("Usage: :resize WxH  (max 16384)");
      return;
    }
    if (layer_count > 1)
      layers_flatten();
    guint32 *before_snap = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    guint32 *np = calloc((size_t)nw * nh, sizeof(guint32));
    if (!before_snap || !np) {
      free(before_snap);
      free(np);
      cmd_flash("Out of memory.");
      return;
    }
    memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    int bw = CANVAS_W, bh = CANVAS_H;
    int cw = MIN(CANVAS_W, nw), ch = MIN(CANVAS_H, nh);
    for (int y = 0; y < ch; y++)
      for (int x = 0; x < cw; x++)
        np[y * nw + x] = PX(y, x);
    free(pixels);
    pixels = np;
    layer_bufs[layer_active] = pixels;
    CANVAS_W = nw;
    CANVAS_H = nh;
    cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
    cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
    visual_anchor_x = CLAMP(visual_anchor_x, 0, CANVAS_W - 1);
    visual_anchor_y = CLAMP(visual_anchor_y, 0, CANVAS_H - 1);
    commit_canvas_snapshot(before_snap, bw, bh);
    zoom_resize();
    gtk_widget_queue_draw(main_canvas);
    if ((size_t)nw * nh > (size_t)4096 * 4096) {
      char wmsg[64];
      snprintf(wmsg, sizeof(wmsg), "Large canvas: ~%zu MB/layer",
               (size_t)nw * nh * 4 / (1024 * 1024));
      cmd_flash(wmsg);
    } else {
      cmd_set("");
    }
    return;
  }

  if (strncmp(cmd_buf, ":savep ", 7) == 0 && *arg) {
    FILE *f = fopen(arg, "w");
    if (!f) {
      cmd_flash("Cannot open file.");
      return;
    }
    for (int i = 0; i < PALETTE_SIZE; i++) {
      int r, g, b;
      palette_to_rgb(i, &r, &g, &b);
      fprintf(f, "#%02x%02x%02x\n", r, g, b);
    }
    fclose(f);
    cmd_flash("Palette saved.");
    return;
  }

  if (strncmp(cmd_buf, ":loadp ", 7) == 0 && *arg) {
    FILE *f = fopen(arg, "r");
    if (!f) {
      cmd_flash("Cannot open file.");
      return;
    }
    int count = 0;
    char line[16];
    while (fgets(line, sizeof(line), f) && count < 256) {
      unsigned int rgb;
      if (line[0] == '#' && sscanf(line + 1, "%06x", &rgb) == 1) {
        if (palette_reserve(count + 1))
          set_palette_rgb(count++, rgb);
      }
    }
    fclose(f);
    if (count == 0) {
      cmd_flash("No colors found.");
      return;
    }
    palette_size = count;
    gtk_widget_queue_draw(palette_bar);
    gtk_widget_queue_draw(main_canvas);
    cmd_flash("Palette loaded.");
    return;
  }

  if (strncmp(cmd_buf, ":importp ", 9) == 0 && *arg) {
    cairo_surface_t *surf = cairo_image_surface_create_from_png(arg);
    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
      cmd_flash("Cannot open file.");
      cairo_surface_destroy(surf);
      return;
    }
    guchar *d = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    int iw = cairo_image_surface_get_width(surf);
    int ih = cairo_image_surface_get_height(surf);
    /* Collect up to 256 unique RGB values from the image */
    unsigned int seen[256];
    int seen_count = 0;
    for (int y = 0; y < ih && seen_count < 256; y++) {
      for (int x = 0; x < iw && seen_count < 256; x++) {
        unsigned int r = d[y * stride + x * 4 + 2];
        unsigned int g = d[y * stride + x * 4 + 1];
        unsigned int b = d[y * stride + x * 4 + 0];
        unsigned int rgb = (r << 16) | (g << 8) | b;
        int dup = 0;
        for (int i = 0; i < seen_count; i++)
          if (seen[i] == rgb) {
            dup = 1;
            break;
          }
        if (!dup)
          seen[seen_count++] = rgb;
      }
    }
    cairo_surface_destroy(surf);
    /* Add colors not already in the palette */
    int added = 0;
    for (int s = 0; s < seen_count && PALETTE_SIZE < 256; s++) {
      unsigned int sr = (seen[s] >> 16) & 0xff;
      unsigned int sg = (seen[s] >> 8) & 0xff;
      unsigned int sb = seen[s] & 0xff;
      int dup = 0;
      for (int i = 0; i < PALETTE_SIZE; i++) {
        int pr, pg, pb;
        palette_to_rgb(i, &pr, &pg, &pb);
        if ((unsigned int)pr == sr && (unsigned int)pg == sg &&
            (unsigned int)pb == sb) {
          dup = 1;
          break;
        }
      }
      if (!dup && palette_reserve(palette_size + 1)) {
        set_palette_rgb(palette_size++, seen[s]);
        added++;
      }
    }
    if (added == 0) {
      cmd_flash("No new colors found.");
    } else {
      char msg[64];
      snprintf(msg, sizeof(msg), "Added %d color(s) to palette.", added);
      gtk_widget_queue_draw(palette_bar);
      gtk_widget_queue_draw(main_canvas);
      cmd_flash(msg);
    }
    return;
  }

  if (strncmp(cmd_buf, ":delp ", 6) == 0 && *arg) {
    int idx = atoi(arg);
    if (idx == 0) {
      cmd_flash("Cannot delete background color (index 0).");
      return;
    }
    if (idx < 0 || idx >= PALETTE_SIZE) {
      cmd_flash("Invalid palette index.");
      return;
    }
    for (int i = idx; i < PALETTE_SIZE - 1; i++) {
      palette[i][0] = palette[i + 1][0];
      palette[i][1] = palette[i + 1][1];
      palette[i][2] = palette[i + 1][2];
    }
    palette_size--;
    gtk_widget_queue_draw(palette_bar);
    gtk_widget_queue_draw(main_canvas);
    cmd_flash("Entry deleted.");
    return;
  }

  if (strcmp(cmd_buf, ":fliph") == 0) {
    begin_undo_action();
    for (int y = 0; y < CANVAS_H; y++)
      for (int x = 0; x < CANVAS_W / 2; x++) {
        push_undo(x, y);
        push_undo(CANVAS_W - 1 - x, y);
      }
    for (int y = 0; y < CANVAS_H; y++)
      for (int x = 0; x < CANVAS_W / 2; x++) {
        guint32 tmp = PX(y, x);
        PX(y, x) = PX(y, CANVAS_W - 1 - x);
        PX(y, CANVAS_W - 1 - x) = tmp;
      }
    commit_undo_action();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strcmp(cmd_buf, ":flipv") == 0) {
    begin_undo_action();
    for (int y = 0; y < CANVAS_H / 2; y++)
      for (int x = 0; x < CANVAS_W; x++) {
        push_undo(x, y);
        push_undo(x, CANVAS_H - 1 - y);
      }
    for (int y = 0; y < CANVAS_H / 2; y++)
      for (int x = 0; x < CANVAS_W; x++) {
        guint32 tmp = PX(y, x);
        PX(y, x) = PX(CANVAS_H - 1 - y, x);
        PX(CANVAS_H - 1 - y, x) = tmp;
      }
    commit_undo_action();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strcmp(cmd_buf, ":rotate") == 0) {
    if (layer_count > 1)
      layers_flatten();
    int bw = CANVAS_W, bh = CANVAS_H;
    guint32 *before_snap = malloc((size_t)bw * bh * sizeof(guint32));
    if (!before_snap) {
      cmd_flash("Out of memory.");
      return;
    }
    memcpy(before_snap, pixels, (size_t)bw * bh * sizeof(guint32));
    int nW = CANVAS_H, nH = CANVAS_W;
    guint32 *np = malloc((size_t)nW * nH * sizeof(guint32));
    if (!np) {
      free(before_snap);
      cmd_flash("Out of memory.");
      return;
    }
    for (int y = 0; y < CANVAS_H; y++)
      for (int x = 0; x < CANVAS_W; x++)
        np[x * nW + (CANVAS_H - 1 - y)] = PX(y, x);
    free(pixels);
    pixels = np;
    layer_bufs[layer_active] = pixels;
    CANVAS_W = nW;
    CANVAS_H = nH;
    cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
    cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
    commit_canvas_snapshot(before_snap, bw, bh);
    zoom_resize();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":export ", 8) == 0 && *arg) {
    /* Parse optional scale: ":export file.png 4" */
    char fname[4096];
    int scale = 1;
    const char *sp = strrchr(arg, ' ');
    if (sp && sp != arg) {
      char *end;
      long sv = strtol(sp + 1, &end, 10);
      if (*end == '\0' && sv >= 1 && sv <= 32) {
        scale = (int)sv;
        size_t flen = (size_t)(sp - arg);
        memcpy(fname, arg, flen);
        fname[flen] = '\0';
        arg = fname;
      }
    }
    size_t alen = strlen(arg);
    if (alen >= 4 && strcmp(arg + alen - 4, ".bmp") == 0) {
      cmd_export_bmp(arg, scale);
    } else if (alen >= 4 && strcmp(arg + alen - 4, ".png") == 0) {
      int sw = CANVAS_W * scale, sh = CANVAS_H * scale;
      cairo_surface_t *surf =
          cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
      guchar *d = cairo_image_surface_get_data(surf);
      int st = cairo_image_surface_get_stride(surf);
      for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++) {
          guint32 px = PX(y, x);
          guchar r = (px >> 24) & 0xff;
          guchar g = (px >> 16) & 0xff;
          guchar b = (px >> 8) & 0xff;
          guchar a = px & 0xff;
          guchar wb, wg, wr, wa;
          if (show_checker && a == 0) {
            wb = wg = wr = wa = 0;
          } else {
            wb = b;
            wg = g;
            wr = r;
            wa = show_checker ? a : 255;
          }
          for (int dy = 0; dy < scale; dy++)
            for (int dx = 0; dx < scale; dx++) {
              int oy = y * scale + dy, ox = x * scale + dx;
              d[oy * st + ox * 4 + 0] = wb;
              d[oy * st + ox * 4 + 1] = wg;
              d[oy * st + ox * 4 + 2] = wr;
              d[oy * st + ox * 4 + 3] = wa;
            }
        }
      cairo_surface_mark_dirty(surf);
      gboolean ok =
          cairo_surface_write_to_png(surf, arg) == CAIRO_STATUS_SUCCESS;
      cairo_surface_destroy(surf);
      cmd_flash(ok ? "Exported." : "Export failed.");
    } else {
      cmd_flash("Unsupported format. Use .png or .bmp.");
    }
    return;
  }

  if (strcmp(cmd_buf, ":center") == 0) {
    /* Find bounding box of non-background pixels */
    int min_x = CANVAS_W, max_x = -1, min_y = CANVAS_H, max_y = -1;
    for (int y = 0; y < CANVAS_H; y++)
      for (int x = 0; x < CANVAS_W; x++)
        if (PX(y, x)) {
          if (x < min_x)
            min_x = x;
          if (x > max_x)
            max_x = x;
          if (y < min_y)
            min_y = y;
          if (y > max_y)
            max_y = y;
        }
    if (max_x < 0) {
      cmd_flash("Nothing to center.");
      return;
    }
    int dx = (CANVAS_W - (max_x - min_x + 1)) / 2 - min_x;
    int dy = (CANVAS_H - (max_y - min_y + 1)) / 2 - min_y;
    if (dx == 0 && dy == 0) {
      cmd_flash("Already centered.");
      return;
    }
    guint32 *before_snap = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    guint32 *np = calloc((size_t)CANVAS_W * CANVAS_H, sizeof(guint32));
    if (!before_snap || !np) {
      free(before_snap);
      free(np);
      cmd_flash("Out of memory.");
      return;
    }
    memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    int bw = CANVAS_W, bh = CANVAS_H;
    for (int y = 0; y < CANVAS_H; y++)
      for (int x = 0; x < CANVAS_W; x++) {
        if (!PX(y, x))
          continue;
        int nx = x + dx, ny = y + dy;
        if (nx >= 0 && nx < CANVAS_W && ny >= 0 && ny < CANVAS_H)
          np[ny * CANVAS_W + nx] = PX(y, x);
      }
    memcpy(pixels, np, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    free(np);
    commit_canvas_snapshot(before_snap, bw, bh);
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strcmp(cmd_buf, ":crop") == 0) {
    if (layer_count > 1)
      layers_flatten();
    int min_x = CANVAS_W, max_x = -1, min_y = CANVAS_H, max_y = -1;
    for (int y = 0; y < CANVAS_H; y++)
      for (int x = 0; x < CANVAS_W; x++)
        if (PX(y, x)) {
          if (x < min_x)
            min_x = x;
          if (x > max_x)
            max_x = x;
          if (y < min_y)
            min_y = y;
          if (y > max_y)
            max_y = y;
        }
    if (max_x < 0) {
      cmd_flash("Nothing to crop.");
      return;
    }
    if (min_x == 0 && min_y == 0 && max_x == CANVAS_W - 1 &&
        max_y == CANVAS_H - 1) {
      cmd_flash("Already at content bounds.");
      return;
    }
    int nW = max_x - min_x + 1, nH = max_y - min_y + 1;
    guint32 *before_snap = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    guint32 *np = calloc((size_t)nW * nH, sizeof(guint32));
    if (!before_snap || !np) {
      free(before_snap);
      free(np);
      cmd_flash("Out of memory.");
      return;
    }
    memcpy(before_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
    int bw = CANVAS_W, bh = CANVAS_H;
    for (int y = min_y; y <= max_y; y++)
      for (int x = min_x; x <= max_x; x++)
        np[(y - min_y) * nW + (x - min_x)] = PX(y, x);
    free(pixels);
    pixels = np;
    layer_bufs[layer_active] = pixels;
    CANVAS_W = nW;
    CANVAS_H = nH;
    cursor_x = CLAMP(cursor_x - min_x, 0, CANVAS_W - 1);
    cursor_y = CLAMP(cursor_y - min_y, 0, CANVAS_H - 1);
    commit_canvas_snapshot(before_snap, bw, bh);
    zoom_resize();
    gtk_widget_queue_draw(main_canvas);
    cmd_set("");
    return;
  }

  if (strncmp(cmd_buf, ":text ", 6) == 0 && *arg) {
    /* Measure actual text extents before allocating the surface so that
       proportional fonts and long strings are never clipped. */
    cairo_surface_t *measure_surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *mcr = cairo_create(measure_surf);
    cairo_select_font_face(mcr, text_font_family, CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(mcr, text_font_size);
    cairo_text_extents_t te;
    cairo_text_extents(mcr, arg, &te);
    cairo_font_extents_t fe_m;
    cairo_font_extents(mcr, &fe_m);
    cairo_destroy(mcr);
    cairo_surface_destroy(measure_surf);
    int sw = (int)(te.x_bearing + te.width + 2);
    int sh = (int)(fe_m.ascent + fe_m.descent + 2);
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    /* Render black text on white ARGB32 surface.
       Checking a color channel (not alpha) gives clean binary results
       because CAIRO_ANTIALIAS_NONE only works reliably on opaque surfaces. */
    cairo_surface_t *tsurf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
    cairo_t *tcr = cairo_create(tsurf);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_NONE);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_FULL);
    cairo_set_font_options(tcr, fo);
    cairo_font_options_destroy(fo);
    cairo_set_source_rgb(tcr, 1, 1, 1);
    cairo_paint(tcr);
    cairo_select_font_face(tcr, text_font_family, CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(tcr, text_font_size);
    cairo_font_extents_t fe;
    cairo_font_extents(tcr, &fe);
    cairo_set_source_rgb(tcr, 0, 0, 0);
    cairo_move_to(tcr, 0, fe.ascent);
    cairo_show_text(tcr, arg);
    cairo_surface_flush(tsurf);
    unsigned char *tdata = cairo_image_surface_get_data(tsurf);
    int tstride = cairo_image_surface_get_stride(tsurf);
    begin_undo_action();
    for (int ty = 0; ty < sh; ty++)
      for (int tx = 0; tx < sw; tx++) {
        /* ARGB32 on LE: [B, G, R, A]; check red channel - 0 = ink, 255 = paper
         */
        unsigned char tr = tdata[ty * tstride + tx * 4 + 2];
        if (tr > 128)
          continue;
        int tcx = cursor_x + tx, tcy = cursor_y + ty;
        if (tcx < 0 || tcx >= CANVAS_W || tcy < 0 || tcy >= CANVAS_H)
          continue;
        push_undo(tcx, tcy);
        PX(tcy, tcx) = fg_color;
      }
    commit_undo_action();
    cairo_destroy(tcr);
    cairo_surface_destroy(tsurf);
    gtk_widget_queue_draw(main_canvas);
    char tmsg[320];
    snprintf(tmsg, sizeof(tmsg), "Text [%s %.4gpt]", text_font_family,
             text_font_size);
    cmd_flash(tmsg);
    return;
  }

  if (strcmp(cmd_buf, ":help") == 0) {
    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
        GTK_BUTTONS_CLOSE, "vim-paint key bindings");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dlg),
        "Movement\n"
        "  h/j/k/l or arrows   move cursor (n=count prefix)\n"
        "  H/J/K/L             jump 5 cells\n"
        "  0 / $               first / last column\n"
        "  gg / G              first / last row\n"
        "\n"
        "Drawing\n"
        "  Space / r           paint rect  (n=radius)\n"
        "  R                   rect outline  (n=radius)\n"
        "  x                   erase rect  (n=radius)\n"
        "  o                   circle outline  (n=radius)\n"
        "  O                   filled circle   (n=radius)\n"
        "  S                   flood fill\n"
        "  i                   insert mode (move to paint)\n"
        "  .                   repeat last paint / erase\n"
        "  :text <string>      stamp text at cursor (8px Monospace)\n"
        "\n"
        "Visual mode  (v)\n"
        "  r / x               fill / erase selection\n"
        "  y / p / P           yank / paste / paste centered\n"
        "  \\                   draw line from anchor to cursor\n"
        "  H / V               flip selection horizontally / vertically\n"
        "  R                   rotate selection 90° clockwise\n"
        "  :scale N            scale selection N× in place (N = 2..8)\n"
        "\n"
        "Palette\n"
        "  c / C               cycle color forward / backward\n"
        "  Ctrl-1..9           select palette slot directly\n"
        "  e                   pick color under cursor\n"
        "  :set color <hex|name>          add/select color\n"
        "  :set color <idx> <hex|name>    edit palette slot\n"
        "  :set bg <hex|name>             set background color\n"
        "  :savep / :loadp <file>         save / load palette\n"
        "  :importp <file>               sample unique colors from PNG\n"
        "  :delp <idx>                    delete palette entry\n"
        "\n"
        "Files\n"
        "  :w [file]   :wq [file]   :e [file]   :new\n"
        "  :tabnew [file]  open file in new tab  (gt / gT to switch)\n"
        "  :export <file>    export as BMP or PNG (by extension)\n"
        "\n"
        "Transform\n"
        "  :resize WxH   :fliph   :flipv   :rotate   :center   :crop\n"
        "  :newlayer              add transparent layer above active\n"
        "  :mergedown             composite active layer into layer below\n"
        "  :layer N               switch to layer N (1-based)\n"
        "  :layervis N            toggle visibility of layer N\n"
        "  :layerblend <mode>     set blend mode (normal|multiply|screen|overlay|...)\n"
        "  :guide h|v N           toggle horizontal/vertical guide at row/col N\n"
        "  :guide clear           remove all guides\n"
        "  :guide snap            toggle snap-to-guides in insert mode\n"
        "\n"
        "View\n"
        "  + / -               zoom in / out\n"
        "  | (pipe)            toggle grid\n"
        "  %                   toggle coordinate ruler\n"
        "  #                   toggle checkerboard background\n"
        "  :set gridcolor <hex|name>  set grid line colour\n"
        "  :set zoom N         set cell size\n"
        "  :set brush N        set brush size (1-16)\n"
        "  :set brushshape square|circle|custom  set brush shape\n"
        "  :brushdefine <pat>  define custom brush (rows sep by /, # = on)\n"
        "  :brushdefine        capture visual selection as custom brush\n"
        "  :set spray <1-100>|off  airbrush density (% pixels per stroke)\n"
        "  :set fill solid|checker|hstripes|vstripes\n"
        "  :set sym h|v|hv|4|none  mirror symmetry\n"
        "  :set sym radial N       N-point radial symmetry (N=2..32)\n"
        "  :set undolevels N   set undo history depth (1-256, clears history)\n"
        "  Ctrl-G              show file info\n"
        "  :goto col,row       jump to position (1-based)\n"
        "  :find color <hex|name>  jump to nearest pixel of color\n"
        "  :replace <from> <to>    replace all pixels of one color with "
        "another\n"
        "  :gradtool [c1 c2]        interactive gradient (click-drag to apply)\n"
        "  :gradient <c1> <c2> h|v  fill canvas with gradient between two "
        "colors\n");
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
    cmd_set("");
    return;
  }

  cmd_flash("Unknown command.");
}

static guint32 *yank_buf = NULL;
static int yank_w = 0;
static int yank_h = 0;

static void free_action(UndoAction *a) {
  free(a->changes);
  a->changes = NULL;
  a->count = 0;
  free(a->before_snap);
  a->before_snap = NULL;
  free(a->after_snap);
  a->after_snap = NULL;
}

static void clear_history(void) {
  for (int i = 0; i < undo_count; i++)
    free_action(
        &undo_stack[(undo_top - undo_count + i + undo_levels * 2) % undo_levels]);
  undo_top = 0;
  undo_count = 0;
  for (int i = 0; i < redo_count; i++)
    free_action(
        &redo_stack[(redo_top - redo_count + i + undo_levels * 2) % undo_levels]);
  redo_top = 0;
  redo_count = 0;
  staged_count = 0;
}

static void begin_undo_action(void) {
  staged_count = 0;
}

static void push_undo(int x, int y) {
  for (int i = 0; i < staged_count; i++)
    if (staged[i].x == x && staged[i].y == y)
      return;
  if (staged_count >= staged_cap) {
    int new_cap = staged_cap ? staged_cap * 2 : 16;
    PixelChange *tmp = realloc(staged, new_cap * sizeof(PixelChange));
    if (!tmp)
      return;
    staged = tmp;
    staged_cap = new_cap;
  }
  staged[staged_count++] = (PixelChange){x, y, PX(y, x), 0};
}

static void commit_undo_action(void) {
  if (staged_count == 0)
    return;
  for (int i = 0; i < redo_count; i++)
    free_action(
        &redo_stack[(redo_top - redo_count + i + undo_levels * 2) % undo_levels]);
  redo_top = 0;
  redo_count = 0;
  for (int i = 0; i < staged_count; i++)
    staged[i].after = PX(staged[i].y, staged[i].x);
  int idx = undo_top % undo_levels;
  if (undo_count == undo_levels)
    free_action(&undo_stack[idx]);
  PixelChange *copy = malloc(staged_count * sizeof(PixelChange));
  if (!copy)
    return;
  memcpy(copy, staged, staged_count * sizeof(PixelChange));
  undo_stack[idx] = (UndoAction){copy, staged_count};
  undo_top++;
  if (undo_count < undo_levels)
    undo_count++;
  staged_count = 0;
  canvas_dirty = TRUE;
  title_refresh();
}

/* Record a full-canvas snapshot undo entry (for ops that change dimensions). */
static void commit_canvas_snapshot(guint32 *before_snap, int bw, int bh) {
  guint32 *after_snap = malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  if (!after_snap) {
    free(before_snap);
    return;
  }
  memcpy(after_snap, pixels, (size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
  for (int i = 0; i < redo_count; i++)
    free_action(
        &redo_stack[(redo_top - redo_count + i + undo_levels * 2) % undo_levels]);
  redo_top = 0;
  redo_count = 0;
  int idx = undo_top % undo_levels;
  if (undo_count == undo_levels)
    free_action(&undo_stack[idx]);
  undo_stack[idx] = (UndoAction){.before_snap = before_snap,
                                 .before_w = bw,
                                 .before_h = bh,
                                 .after_snap = after_snap,
                                 .after_w = CANVAS_W,
                                 .after_h = CANVAS_H};
  undo_top++;
  if (undo_count < undo_levels)
    undo_count++;
  canvas_dirty = TRUE;
  title_refresh();
}

static void paint_pixel_raw(int x, int y, guint32 color) {
  if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H)
    return;
  push_undo(x, y);
  PX(y, x) = color;
}

static void paint_pixel(int x, int y, guint32 color) {
  if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H)
    return;
  paint_pixel_raw(x, y, color);
  switch (sym_mode) {
    case SYM_H: {
      int mx = CANVAS_W - 1 - x;
      if (mx != x) paint_pixel_raw(mx, y, color);
      break;
    }
    case SYM_V: {
      int my = CANVAS_H - 1 - y;
      if (my != y) paint_pixel_raw(x, my, color);
      break;
    }
    case SYM_HV: {
      int mx = CANVAS_W - 1 - x, my = CANVAS_H - 1 - y;
      if (mx != x) paint_pixel_raw(mx, y, color);
      if (my != y) paint_pixel_raw(x, my, color);
      if (mx != x && my != y) paint_pixel_raw(mx, my, color);
      break;
    }
    case SYM_RADIAL: {
      double cx = (CANVAS_W - 1) / 2.0, cy = (CANVAS_H - 1) / 2.0;
      double rx = x - cx, ry = y - cy;
      double r = sqrt(rx * rx + ry * ry);
      double theta = atan2(ry, rx);
      double step = 2.0 * M_PI / sym_radial_n;
      for (int k = 1; k < sym_radial_n; k++) {
        double a = theta + k * step;
        int px = (int)(cx + r * cos(a) + 0.5);
        int py = (int)(cy + r * sin(a) + 0.5);
        if (px != x || py != y)
          paint_pixel_raw(px, py, color);
      }
      break;
    }
    default:
      break;
  }
}

static void paint_brush(int x, int y, guint32 color) {
  if (brush_shape == 2 && custom_brush_w > 0 && custom_brush_h > 0) {
    int ox = custom_brush_w / 2, oy = custom_brush_h / 2;
    for (int dy = 0; dy < custom_brush_h; dy++)
      for (int dx = 0; dx < custom_brush_w; dx++) {
        if (!custom_brush_pixels[dy][dx])
          continue;
        if (spray_density > 0 && (rand() % 100) >= spray_density)
          continue;
        paint_pixel(x - ox + dx, y - oy + dy, color);
      }
    return;
  }
  int half = brush_size / 2;
  for (int dy = 0; dy < brush_size; dy++)
    for (int dx = 0; dx < brush_size; dx++) {
      if (brush_shape == 1) {
        int cdx = dx - half, cdy = dy - half;
        if (cdx * cdx + cdy * cdy > half * half)
          continue;
      }
      if (spray_density > 0 && (rand() % 100) >= spray_density)
        continue;
      paint_pixel(x - half + dx, y - half + dy, color);
    }
}

static void find_right(void) {
  for (int fx = cursor_x + 1; fx < CANVAS_W; fx++)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
  for (int fx = 0; fx < cursor_x; fx++)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
}
static void find_left(void) {
  for (int fx = cursor_x - 1; fx >= 0; fx--)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
  for (int fx = CANVAS_W - 1; fx > cursor_x; fx--)
    if (PX(cursor_y, fx)) {
      cursor_x = fx;
      return;
    }
}
static void find_down(void) {
  for (int fy = cursor_y + 1; fy < CANVAS_H; fy++)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
  for (int fy = 0; fy < cursor_y; fy++)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
}
static void find_up(void) {
  for (int fy = cursor_y - 1; fy >= 0; fy--)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
  for (int fy = CANVAS_H - 1; fy > cursor_y; fy--)
    if (PX(fy, cursor_x)) {
      cursor_y = fy;
      return;
    }
}

/* Return the fill color for pixel (x,y) given current fill_pattern.
   fg is the primary color, bg_color is the secondary (transparent pixels
   in pattern positions use bg_color with alpha=0 if bg is transparent). */
static guint32 fill_color_at(int x, int y, guint32 fg) {
  switch (fill_pattern) {
    case FILL_CHECKER:
      return ((x + y) & 1) ? bg_color : fg;
    case FILL_HSTRIPES:
      return (y & 1) ? bg_color : fg;
    case FILL_VSTRIPES:
      return (x & 1) ? bg_color : fg;
    case FILL_HALFTONE: {
      /* 4x4 ordered-dither Bayer matrix at ~50% density */
      static const int bayer[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5}
      };
      return bayer[y & 3][x & 3] < 4 ? fg : bg_color;
    }
    default:
      return fg;
  }
}

static void flood_fill(int sx, int sy, guint32 fill_color) {
  guint32 target = PX(sy, sx);
  if (fill_pattern == FILL_SOLID && target == fill_color)
    return;
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *before_snap = malloc(total * sizeof(guint32));
  int *queue = malloc(total * sizeof(int));
  /* visited prevents re-queuing when bg_color == target under pattern fills */
  gboolean *visited = calloc(total, sizeof(gboolean));
  if (!before_snap || !queue || !visited) {
    free(before_snap);
    free(queue);
    free(visited);
    return;
  }
  memcpy(before_snap, pixels, total * sizeof(guint32));
  int head = 0, tail = 0;
  visited[sy * CANVAS_W + sx] = TRUE;
  queue[tail++] = sy * CANVAS_W + sx;
  PX(sy, sx) = fill_color_at(sx, sy, fill_color);
  while (head < tail) {
    int pos = queue[head++];
    int x = pos % CANVAS_W, y = pos / CANVAS_W;
    int neighbors[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
    for (int i = 0; i < 4; i++) {
      int nx = neighbors[i][0], ny = neighbors[i][1];
      if (nx < 0 || nx >= CANVAS_W || ny < 0 || ny >= CANVAS_H)
        continue;
      int npos = ny * CANVAS_W + nx;
      if (visited[npos] || PX(ny, nx) != target)
        continue;
      visited[npos] = TRUE;
      PX(ny, nx) = fill_color_at(nx, ny, fill_color);
      queue[tail++] = npos;
    }
  }
  free(visited);
  free(queue);
  commit_canvas_snapshot(before_snap, CANVAS_W, CANVAS_H);
}

/* Snap coord to nearest guide on that axis; returns snapped value. */
static int snap_coord(int coord, gboolean horizontal) {
  if (!guide_snap || guide_count == 0)
    return coord;
  int best = coord, bestd = 3; /* snap radius: 3 cells */
  for (int i = 0; i < guide_count; i++) {
    if (guides[i].horizontal != horizontal)
      continue;
    int d = guides[i].coord - coord;
    if (d < 0) d = -d;
    if (d < bestd) { bestd = d; best = guides[i].coord; }
  }
  return best;
}

static void apply_gradient_linear(int x0, int y0, int x1, int y1,
                                   guint32 c1, guint32 c2) {
  int r1 = (c1 >> 16) & 0xff, g1 = (c1 >> 8) & 0xff, b1 = c1 & 0xff;
  int r2 = (c2 >> 16) & 0xff, g2 = (c2 >> 8) & 0xff, b2 = c2 & 0xff;
  double dx = x1 - x0, dy = y1 - y0;
  double len2 = dx * dx + dy * dy;
  int rx0 = 0, ry0 = 0, rx1 = CANVAS_W - 1, ry1 = CANVAS_H - 1;
  if (visual_mode) {
    rx0 = MIN(cursor_x, visual_anchor_x); rx1 = MAX(cursor_x, visual_anchor_x);
    ry0 = MIN(cursor_y, visual_anchor_y); ry1 = MAX(cursor_y, visual_anchor_y);
  }
  begin_undo_action();
  for (int y = ry0; y <= ry1; y++) {
    for (int x = rx0; x <= rx1; x++) {
      double t = 0.0;
      if (len2 > 0.5)
        t = ((x - x0) * dx + (y - y0) * dy) / len2;
      t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
      int ri = (int)(r1 + t * (r2 - r1) + 0.5);
      int gi = (int)(g1 + t * (g2 - g1) + 0.5);
      int bi = (int)(b1 + t * (b2 - b1) + 0.5);
      push_undo(x, y);
      PX(y, x) = PACK_RGBA(CLAMP(ri, 0, 255), CLAMP(gi, 0, 255),
                            CLAMP(bi, 0, 255), 255);
    }
  }
  commit_undo_action();
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *composite = malloc(total * sizeof(guint32));
  if (composite)
    layers_composite(composite, total);
  for (int y = 0; y < CANVAS_H; y++) {
    for (int x = 0; x < CANVAS_W; x++) {
      guint32 px = composite ? composite[y * CANVAS_W + x] : PX(y, x);
      guchar r = (px >> 24) & 0xff;
      guchar g = (px >> 16) & 0xff;
      guchar b = (px >> 8) & 0xff;
      guchar a = px & 0xff;
      if (show_checker && a == 0) {
        /* Alternating light/dark squares to indicate transparent background */
        if ((x + y) % 2 == 0)
          cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        else
          cairo_set_source_rgb(cr, 0.78, 0.78, 0.78);
      } else {
        cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
      }
      cairo_rectangle(cr, x * CELL_SIZE, y * CELL_SIZE, CELL_SIZE, CELL_SIZE);
      cairo_fill(cr);
    }
  }
  free(composite);

  /* Draw grid lines */
  if (show_grid) {
    cairo_set_source_rgba(cr,
                          ((grid_color >> 24) & 0xff) / 255.0,
                          ((grid_color >> 16) & 0xff) / 255.0,
                          ((grid_color >>  8) & 0xff) / 255.0,
                          ( grid_color        & 0xff) / 255.0);
    cairo_set_line_width(cr, 0.5);
    for (int x = 0; x <= CANVAS_W; x++) {
      cairo_move_to(cr, x * CELL_SIZE, 0);
      cairo_line_to(cr, x * CELL_SIZE, CANVAS_H * CELL_SIZE);
    }
    for (int y = 0; y <= CANVAS_H; y++) {
      cairo_move_to(cr, 0, y * CELL_SIZE);
      cairo_line_to(cr, CANVAS_W * CELL_SIZE, y * CELL_SIZE);
    }
    cairo_stroke(cr);
  }

  /* Draw visual selection highlight */
  if (visual_mode) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    cairo_set_source_rgba(cr, 0.2, 0.4, 1.0, 0.35);
    cairo_rectangle(cr, x0 * CELL_SIZE, y0 * CELL_SIZE,
                    (x1 - x0 + 1) * CELL_SIZE, (y1 - y0 + 1) * CELL_SIZE);
    cairo_fill(cr);
  }

  /* Draw cursor: filled green in insert mode, outlined red/white in normal mode
   */
  if (insert_mode) {
    cairo_set_source_rgba(cr, 0.0, 0.85, 0.3, 0.55);
    cairo_rectangle(cr, cursor_x * CELL_SIZE, cursor_y * CELL_SIZE, CELL_SIZE,
                    CELL_SIZE);
    cairo_fill(cr);
  } else {
    {
      guint32 pv = PX(cursor_y, cursor_x);
      int pr = (pv >> 24) & 0xff;
      int pg = (pv >> 16) & 0xff;
      int pb = (pv >> 8) & 0xff;
      int lum = (pr * 299 + pg * 587 + pb * 114) / 1000;
      if (lum > 128)
        cairo_set_source_rgb(cr, 1, 0, 0); /* red on bright pixels */
      else
        cairo_set_source_rgb(cr, 1, 1, 1); /* white on dark pixels */
    }
    double lw = CLAMP(CELL_SIZE * 0.2, 1.0, 2.0);
    int inset = MAX(1, CELL_SIZE / 8);
    cairo_set_line_width(cr, lw);
    cairo_rectangle(cr, cursor_x * CELL_SIZE + inset,
                    cursor_y * CELL_SIZE + inset, CELL_SIZE - 2 * inset,
                    CELL_SIZE - 2 * inset);
    cairo_stroke(cr);
  }

  /* Draw gradient tool preview: line + endpoint markers */
  if (gradient_tool && grad_dragging) {
    double sx = grad_x0 * CELL_SIZE + CELL_SIZE / 2.0;
    double sy = grad_y0 * CELL_SIZE + CELL_SIZE / 2.0;
    double ex = cursor_x * CELL_SIZE + CELL_SIZE / 2.0;
    double ey = cursor_y * CELL_SIZE + CELL_SIZE / 2.0;
    cairo_set_line_width(cr, 2.0);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
    cairo_move_to(cr, sx + 1, sy + 1);
    cairo_line_to(cr, ex + 1, ey + 1);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    cairo_move_to(cr, sx, sy);
    cairo_line_to(cr, ex, ey);
    cairo_stroke(cr);
    /* Color circles at endpoints */
    double r1 = (grad_c1 >> 16 & 0xff) / 255.0;
    double g1 = (grad_c1 >> 8  & 0xff) / 255.0;
    double b1 = (grad_c1       & 0xff) / 255.0;
    double r2 = (grad_c2 >> 16 & 0xff) / 255.0;
    double g2 = (grad_c2 >> 8  & 0xff) / 255.0;
    double b2 = (grad_c2       & 0xff) / 255.0;
    double rad = CLAMP(CELL_SIZE * 0.4, 3.0, 8.0);
    cairo_arc(cr, sx, sy, rad, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, r1, g1, b1);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_arc(cr, ex, ey, rad, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, r2, g2, b2);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
    cairo_stroke(cr);
  }

  /* Draw guide lines */
  if (guide_count > 0) {
    double cw = CANVAS_W * CELL_SIZE, ch = CANVAS_H * CELL_SIZE;
    cairo_set_line_width(cr, 1.0);
    for (int i = 0; i < guide_count; i++) {
      double pos = guides[i].coord * CELL_SIZE;
      /* Cyan with a dark shadow for visibility on any background */
      cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
      if (guides[i].horizontal) {
        cairo_move_to(cr, 0.5, pos + 0.5);
        cairo_line_to(cr, cw + 0.5, pos + 0.5);
      } else {
        cairo_move_to(cr, pos + 0.5, 0.5);
        cairo_line_to(cr, pos + 0.5, ch + 0.5);
      }
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, 0.0, 0.85, 1.0, 0.75);
      if (guides[i].horizontal) {
        cairo_move_to(cr, 0, pos);
        cairo_line_to(cr, cw, pos);
      } else {
        cairo_move_to(cr, pos, 0);
        cairo_line_to(cr, pos, ch);
      }
      cairo_stroke(cr);
    }
  }

  /* Draw coordinate ruler overlay */
  if (show_ruler) {
    int step = 1;
    if (CELL_SIZE < 20)
      step = 5;
    if (CELL_SIZE < 8)
      step = 10;
    if (CELL_SIZE < 4)
      step = 20;
    double fsize = CLAMP(CELL_SIZE * 0.75, 5.0, 11.0);
    double baseline = CLAMP(fsize, 5.0, (double)CELL_SIZE - 1.0);
    double tick = CLAMP(CELL_SIZE * 0.25, 2.0, 5.0);
    cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, fsize);
    cairo_set_line_width(cr, 1.0);
    for (int x = 0; x < CANVAS_W; x += step) {
      double px = x * CELL_SIZE;
      /* Tick mark at top edge of column */
      cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
      cairo_move_to(cr, px + 0.5, 0.5);
      cairo_line_to(cr, px + 0.5, tick + 0.5);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
      cairo_move_to(cr, px, 0);
      cairo_line_to(cr, px, tick);
      cairo_stroke(cr);
      /* Coordinate label */
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", x + 1);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.65);
      cairo_move_to(cr, px + 1.5, baseline + 0.5);
      cairo_show_text(cr, buf);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
      cairo_move_to(cr, px + 1, baseline);
      cairo_show_text(cr, buf);
    }
    for (int y = 0; y < CANVAS_H; y += step) {
      double py = y * CELL_SIZE;
      /* Tick mark at left edge of row */
      cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
      cairo_move_to(cr, 0.5, py + 0.5);
      cairo_line_to(cr, tick + 0.5, py + 0.5);
      cairo_stroke(cr);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
      cairo_move_to(cr, 0, py);
      cairo_line_to(cr, tick, py);
      cairo_stroke(cr);
      /* Coordinate label */
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", y + 1);
      cairo_set_source_rgba(cr, 0, 0, 0, 0.65);
      cairo_move_to(cr, 1.5, py + baseline + 0.5);
      cairo_show_text(cr, buf);
      cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
      cairo_move_to(cr, 1, py + baseline);
      cairo_show_text(cr, buf);
    }
  }

  return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer data) {
  if (cmd_mode) {
    if (event->keyval == GDK_KEY_Escape) {
      tab_reset();
      cmd_mode = FALSE;
      status_update();
    } else if (event->keyval == GDK_KEY_Return) {
      tab_reset();
      cmd_mode = FALSE;
      if (cmd_len > 1) {
        if (cmd_history_count == 0 ||
            strcmp(cmd_history[(cmd_history_count - 1) % CMD_HISTORY_MAX],
                   cmd_buf) != 0) {
          snprintf(cmd_history[cmd_history_count % CMD_HISTORY_MAX],
                   sizeof(cmd_buf), "%s", cmd_buf);
          cmd_history_count++;
        }
      }
      cmd_history_idx = -1;
      cmd_history_draft[0] = '\0';
      cmd_execute();
    } else if (event->keyval == GDK_KEY_Up) {
      tab_reset();
      if (cmd_history_count > 0) {
        if (cmd_history_idx == -1)
          snprintf(cmd_history_draft, sizeof(cmd_history_draft), "%s", cmd_buf);
        int next = cmd_history_idx + 1;
        int avail = cmd_history_count < CMD_HISTORY_MAX ? cmd_history_count
                                                        : CMD_HISTORY_MAX;
        if (next < avail) {
          cmd_history_idx = next;
          int hidx =
              (cmd_history_count - 1 - cmd_history_idx) % CMD_HISTORY_MAX;
          snprintf(cmd_buf, sizeof(cmd_buf), "%s", cmd_history[hidx]);
          cmd_len = strlen(cmd_buf);
          cmd_set(cmd_buf);
        }
      }
    } else if (event->keyval == GDK_KEY_Down) {
      tab_reset();
      if (cmd_history_idx > 0) {
        cmd_history_idx--;
        int hidx = (cmd_history_count - 1 - cmd_history_idx) % CMD_HISTORY_MAX;
        snprintf(cmd_buf, sizeof(cmd_buf), "%s", cmd_history[hidx]);
        cmd_len = strlen(cmd_buf);
        cmd_set(cmd_buf);
      } else if (cmd_history_idx == 0) {
        cmd_history_idx = -1;
        snprintf(cmd_buf, sizeof(cmd_buf), "%s", cmd_history_draft);
        cmd_len = strlen(cmd_buf);
        cmd_set(cmd_buf);
      }
    } else if (event->keyval == GDK_KEY_BackSpace) {
      tab_reset();
      if (cmd_len > 1) {
        cmd_buf[--cmd_len] = '\0';
        cmd_set(cmd_buf);
      }
    } else if (event->keyval == GDK_KEY_Tab) {
      static const char *const file_pfxs[] = {
          ":loadp ", ":savep ", ":export ", ":importp ", ":wq ",
          ":w ",     ":e! ",    ":e ",      NULL};
      const char *pfx = NULL;
      for (int i = 0; file_pfxs[i]; i++) {
        size_t plen = strlen(file_pfxs[i]);
        if (strncmp(cmd_buf, file_pfxs[i], plen) == 0) {
          pfx = file_pfxs[i];
          break;
        }
      }
      if (pfx) {
        if (!tab_glob_valid) {
          const char *partial = cmd_buf + strlen(pfx);
          char pattern[sizeof(cmd_buf) + 2];
          snprintf(pattern, sizeof(pattern), "%s*", partial);
          memset(&tab_glob, 0, sizeof(tab_glob));
          if (glob(pattern, GLOB_MARK | GLOB_TILDE, NULL, &tab_glob) == 0 &&
              tab_glob.gl_pathc > 0) {
            tab_glob_valid = TRUE;
            tab_glob_idx = 0;
            snprintf(tab_cmd_prefix, sizeof(tab_cmd_prefix), "%s", pfx);
          } else {
            globfree(&tab_glob);
          }
        } else {
          tab_glob_idx = (tab_glob_idx + 1) % (int)tab_glob.gl_pathc;
        }
        if (tab_glob_valid) {
          int written =
              snprintf(cmd_buf, sizeof(cmd_buf), "%s%s", tab_cmd_prefix,
                       tab_glob.gl_pathv[tab_glob_idx]);
          if (written >= (int)sizeof(cmd_buf)) {
            tab_reset();
            cmd_flash("Path too long.");
          } else {
            cmd_len = strlen(cmd_buf);
            cmd_set(cmd_buf);
          }
        }
      } else {
        /* Color name completion for :set bg, :set color, :find color */
        static const char *const color_pfxs[] = {":set bg ", ":set color ",
                                                 ":find color ", NULL};
        const char *cpfx = NULL;
        for (int i = 0; color_pfxs[i]; i++) {
          size_t plen = strlen(color_pfxs[i]);
          if (strncmp(cmd_buf, color_pfxs[i], plen) == 0) {
            cpfx = color_pfxs[i];
            break;
          }
        }
        /* Also match :set color <N> <partial> */
        char dyn_pfx[64] = "";
        if (!cpfx && strncmp(cmd_buf, ":set color ", 11) == 0) {
          const char *after = cmd_buf + 11;
          if (*after >= '0' && *after <= '9') {
            const char *sp = strchr(after, ' ');
            if (sp) {
              size_t plen = (size_t)(sp + 1 - cmd_buf);
              if (plen < sizeof(dyn_pfx)) {
                strncpy(dyn_pfx, cmd_buf, plen);
                dyn_pfx[plen] = '\0';
                cpfx = dyn_pfx;
              }
            }
          }
        }
        if (cpfx) {
          const char *partial = cmd_buf + strlen(cpfx);
          if (!color_tab_valid) {
            color_tab_count = 0;
            color_tab_idx = 0;
            size_t plen = strlen(partial);
            for (int i = 0; i < NAMED_COLORS_COUNT; i++)
              if (color_tab_count < 256 &&
                  strncmp(named_colors[i].name, partial, plen) == 0)
                color_tab_matches[color_tab_count++] = i;
            if (color_tab_count > 0) {
              color_tab_valid = TRUE;
              snprintf(color_tab_prefix, sizeof(color_tab_prefix), "%s", cpfx);
            }
          } else {
            color_tab_idx = (color_tab_idx + 1) % color_tab_count;
          }
          if (color_tab_valid) {
            int written =
                snprintf(cmd_buf, sizeof(cmd_buf), "%s%s", color_tab_prefix,
                         named_colors[color_tab_matches[color_tab_idx]].name);
            if (written >= (int)sizeof(cmd_buf)) {
              color_tab_valid = FALSE;
              color_tab_count = 0;
              cmd_flash("Color name too long.");
            } else {
              cmd_len = strlen(cmd_buf);
              cmd_set(cmd_buf);
            }
          }
        }
      }
    } else {
      tab_reset();
      guint32 uc = gdk_keyval_to_unicode(event->keyval);
      if (uc >= 0x20 && uc < 0x7f && cmd_len < (int)sizeof(cmd_buf) - 2) {
        cmd_buf[cmd_len++] = (char)uc;
        cmd_buf[cmd_len] = '\0';
        cmd_set(cmd_buf);
      }
    }
    return TRUE;
  }

  if (event->is_modifier)
    return FALSE;

  static gboolean pending_g = FALSE;
  static gboolean pending_d = FALSE;
  static gboolean pending_f = FALSE;
  static gboolean pending_q = FALSE;
  static gboolean pending_at = FALSE;
  static int d_count = 1;
  static int f_count = 1;

  /* Stop recording: q while already recording */
  if (macro_recording && event->keyval == GDK_KEY_q &&
      !(event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK))) {
    macro_recording = FALSE;
    char msg[40];
    snprintf(msg, sizeof(msg), "Recorded @%c  (%d events)", 'a' + macro_reg,
             macro_len[macro_reg]);
    cmd_flash(msg);
    return TRUE;
  }

  /* Append to macro buffer while recording */
  if (macro_recording && !macro_playing) {
    if (macro_len[macro_reg] < MACRO_MAX_EVENTS) {
      macro_buf[macro_reg][macro_len[macro_reg]].keyval = event->keyval;
      macro_buf[macro_reg][macro_len[macro_reg]].state = event->state;
      macro_len[macro_reg]++;
    } else {
      cmd_flash("Macro buffer full.");
    }
  }

  /* q<letter>: start recording */
  if (pending_q) {
    pending_q = FALSE;
    if (event->keyval >= GDK_KEY_a && event->keyval <= GDK_KEY_z) {
      macro_reg = event->keyval - GDK_KEY_a;
      macro_len[macro_reg] = 0;
      macro_recording = TRUE;
      status_update();
    }
    return TRUE;
  }

  /* @<letter>: replay macro */
  if (pending_at) {
    pending_at = FALSE;
    if (!macro_playing && event->keyval >= GDK_KEY_a &&
        event->keyval <= GDK_KEY_z) {
      int reg = event->keyval - GDK_KEY_a;
      if (macro_len[reg] > 0) {
        macro_playing = TRUE;
        GdkEventKey fake = *event;
        fake.send_event = TRUE;
        for (int i = 0; i < macro_len[reg]; i++) {
          fake.keyval = macro_buf[reg][i].keyval;
          fake.state = macro_buf[reg][i].state;
          on_key_press(widget, &fake, data);
        }
        macro_playing = FALSE;
        gtk_widget_queue_draw(main_canvas);
      }
    }
    return TRUE;
  }

  if (event->keyval == GDK_KEY_colon) {
    pending_g = FALSE;
    pending_d = FALSE;
    pending_f = FALSE;
    pending_q = FALSE;
    pending_at = FALSE;
    cmd_mode = TRUE;
    cmd_buf[0] = ':';
    cmd_buf[1] = '\0';
    cmd_len = 1;
    cmd_set(cmd_buf);
    return TRUE;
  }
  static int count = 0;
  static guint last_action = 0;
  static int last_radius = 0;
  static gboolean last_was_visual = FALSE;
  static int last_visual_dx = 0, last_visual_dy = 0;
  static int last_find_dir =
      0; /* +1 = forward (f), -1 = backward (F), 0 = none */
  static int gg_count = 0;

  if (pending_g) {
    pending_g = FALSE;
    if (event->keyval == GDK_KEY_g) {
      cursor_y = gg_count > 0 ? CLAMP(gg_count - 1, 0, CANVAS_H - 1) : 0;
      count = 0;
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    if (event->keyval == GDK_KEY_t) {
      tab_switch((tab_current + 1) % tab_count);
      return TRUE;
    }
    if (event->keyval == GDK_KEY_T) {
      tab_switch((tab_current - 1 + tab_count) % tab_count);
      return TRUE;
    }
  }

  if (pending_d) {
    pending_d = FALSE;
    int x0 = cursor_x, x1 = cursor_x;
    int y0 = cursor_y, y1 = cursor_y;
    gboolean whole_row = FALSE;
    switch (event->keyval) {
    case GDK_KEY_d:
      whole_row = TRUE;
      break;
    case GDK_KEY_h:
      x0 = MAX(cursor_x - d_count, 0);
      break;
    case GDK_KEY_l:
      x1 = MIN(cursor_x + d_count, CANVAS_W - 1);
      break;
    case GDK_KEY_w:
      x1 = MIN(cursor_x + 5 * d_count, CANVAS_W - 1);
      break;
    case GDK_KEY_b:
      x0 = MAX(cursor_x - 5 * d_count, 0);
      break;
    case GDK_KEY_0:
      x0 = 0;
      break;
    case GDK_KEY_dollar:
      x1 = CANVAS_W - 1;
      break;
    case GDK_KEY_j:
      whole_row = TRUE;
      y1 = MIN(cursor_y + d_count, CANVAS_H - 1);
      break;
    case GDK_KEY_k:
      whole_row = TRUE;
      y0 = MAX(cursor_y - d_count, 0);
      break;
    default:
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    begin_undo_action();
    if (whole_row) {
      for (int ey = y0; ey <= y1; ey++)
        for (int ex = 0; ex < CANVAS_W; ex++)
          paint_pixel(ex, ey, bg_color);
    } else {
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, cursor_y, bg_color);
    }
    commit_undo_action();
    last_action = GDK_KEY_x;
    last_was_visual = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (pending_f) {
    pending_f = FALSE;
    void (*fn)(void) = find_right;
    if (event->keyval == GDK_KEY_j) {
      last_find_dir = 2;
      fn = find_down;
    } else if (event->keyval == GDK_KEY_k) {
      last_find_dir = -2;
      fn = find_up;
    } else if (event->keyval == GDK_KEY_h) {
      last_find_dir = -1;
      fn = find_left;
    } else {
      last_find_dir = 1;
    }
    for (int i = 0; i < f_count; i++)
      fn();
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  /* Ctrl+1–9: select palette slot directly */
  if ((event->state & GDK_CONTROL_MASK) && event->keyval >= GDK_KEY_1 &&
      event->keyval <= GDK_KEY_9) {
    int slot = event->keyval - GDK_KEY_0;
    if (slot < PALETTE_SIZE) {
      int pr, pg, pb;
      palette_to_rgb(slot, &pr, &pg, &pb);
      fg_color = PACK_RGBA(pr, pg, pb, 255);
      gtk_widget_queue_draw(palette_bar);
      flash_color(slot);
    } else {
      cmd_flash("No such palette slot.");
    }
    return TRUE;
  }

  /* Accumulate numeric prefix; treat 0 as line-start only when count is 0 */
  if (event->keyval >= GDK_KEY_1 && event->keyval <= GDK_KEY_9) {
    count = count * 10 + (event->keyval - GDK_KEY_0);
    return TRUE;
  }
  if (event->keyval == GDK_KEY_0 && count > 0) {
    count = count * 10;
    return TRUE;
  }

  int orig_count = count;
  int n = count > 0 ? count : 1;
  int radius = count;
  count = 0;

  if (event->keyval == GDK_KEY_v) {
    visual_mode = !visual_mode;
    visual_anchor_x = cursor_x;
    visual_anchor_y = cursor_y;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (event->keyval == GDK_KEY_Escape) {
    visual_mode = FALSE;
    insert_mode = FALSE;
    gradient_tool = FALSE;
    grad_dragging = FALSE;
    pending_g = FALSE;
    pending_d = FALSE;
    pending_f = FALSE;
    pending_q = FALSE;
    pending_at = FALSE;
    macro_recording = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_y) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    yank_w = x1 - x0 + 1;
    yank_h = y1 - y0 + 1;
    free(yank_buf);
    yank_buf = malloc(yank_w * yank_h * sizeof(guint32));
    if (!yank_buf) {
      cmd_flash("Out of memory.");
      visual_mode = FALSE;
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    for (int ey = 0; ey < yank_h; ey++)
      for (int ex = 0; ex < yank_w; ex++)
        yank_buf[ey * yank_w + ex] = PX(y0 + ey, x0 + ex);
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode &&
      (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_x)) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    guint32 val = (event->keyval == GDK_KEY_r) ? fg_color : bg_color;
    begin_undo_action();
    for (int ey = y0; ey <= y1; ey++)
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, ey, val);
    commit_undo_action();
    last_action = event->keyval;
    last_was_visual = TRUE;
    last_visual_dx = visual_anchor_x - cursor_x;
    last_visual_dy = visual_anchor_y - cursor_y;
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_backslash) {
    begin_undo_action();
    draw_line(visual_anchor_x, visual_anchor_y, cursor_x, cursor_y, fg_color);
    commit_undo_action();
    last_action = GDK_KEY_backslash;
    last_was_visual = TRUE;
    last_visual_dx = visual_anchor_x - cursor_x;
    last_visual_dy = visual_anchor_y - cursor_y;
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode &&
      (event->keyval == GDK_KEY_H || event->keyval == GDK_KEY_V)) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    begin_undo_action();
    if (event->keyval == GDK_KEY_H) {
      /* Flip selection horizontally */
      for (int ey = y0; ey <= y1; ey++) {
        for (int ex = x0; ex <= x0 + (x1 - x0) / 2; ex++) {
          int ex2 = x1 - (ex - x0);
          if (ex == ex2)
            continue;
          push_undo(ex, ey);
          push_undo(ex2, ey);
          guint32 tmp = PX(ey, ex);
          PX(ey, ex) = PX(ey, ex2);
          PX(ey, ex2) = tmp;
        }
      }
    } else {
      /* Flip selection vertically */
      for (int ey = y0; ey <= y0 + (y1 - y0) / 2; ey++) {
        int ey2 = y1 - (ey - y0);
        if (ey == ey2)
          continue;
        for (int ex = x0; ex <= x1; ex++) {
          push_undo(ex, ey);
          push_undo(ex, ey2);
          guint32 tmp = PX(ey, ex);
          PX(ey, ex) = PX(ey2, ex);
          PX(ey2, ex) = tmp;
        }
      }
    }
    commit_undo_action();
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (visual_mode && event->keyval == GDK_KEY_R) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    int W = x1 - x0 + 1, H = y1 - y0 + 1;
    guint32 *tmp = malloc((size_t)W * H * sizeof(guint32));
    if (!tmp) {
      cmd_flash("Out of memory.");
      visual_mode = FALSE;
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
      return TRUE;
    }
    /* 90° CW: dst[dr][dc] = src[H-1-dc][dr], new dims = H wide × W tall */
    for (int dr = 0; dr < W; dr++)
      for (int dc = 0; dc < H; dc++)
        tmp[dr * H + dc] = PX(y0 + (H - 1 - dc), x0 + dr);
    begin_undo_action();
    for (int dr = 0; dr < W && y0 + dr < CANVAS_H; dr++)
      for (int dc = 0; dc < H && x0 + dc < CANVAS_W; dc++)
        paint_pixel(x0 + dc, y0 + dr, tmp[dr * H + dc]);
    free(tmp);
    commit_undo_action();
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
  }

  if (event->keyval == GDK_KEY_s && (event->state & GDK_CONTROL_MASK)) {
    if (*last_filename)
      cmd_write(last_filename);
    else
      cmd_flash("No filename. Use :w filename");
    return TRUE;
  }

  if (event->keyval == GDK_KEY_g && (event->state & GDK_CONTROL_MASK)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "\"%s\"  %dx%d  col: %d  row: %d",
             *last_filename ? last_filename : "[No Name]", CANVAS_W, CANVAS_H,
             cursor_x + 1, cursor_y + 1);
    cmd_flash(buf);
    return TRUE;
  }

  if (event->keyval == GDK_KEY_r && (event->state & GDK_CONTROL_MASK)) {
    if (redo_count > 0) {
      redo_top--;
      redo_count--;
      int ridx = redo_top % undo_levels;
      UndoAction a = redo_stack[ridx];
      redo_stack[ridx] = (UndoAction){NULL, 0};
      int uidx = undo_top % undo_levels;
      if (undo_count == undo_levels)
        free_action(&undo_stack[uidx]);
      undo_stack[uidx] = a;
      undo_top++;
      if (undo_count < undo_levels)
        undo_count++;
      if (a.after_snap) {
        guint32 *np = malloc(a.after_w * a.after_h * sizeof(guint32));
        if (np) {
          memcpy(np, a.after_snap, a.after_w * a.after_h * sizeof(guint32));
          free(pixels);
          pixels = np;
          layer_bufs[layer_active] = pixels;
          CANVAS_W = a.after_w;
          CANVAS_H = a.after_h;
          cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
          cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
          zoom_resize();
        }
      } else {
        for (int i = 0; i < a.count; i++) {
          PX(a.changes[i].y, a.changes[i].x) = a.changes[i].after;
          cursor_x = a.changes[i].x;
          cursor_y = a.changes[i].y;
        }
      }
      status_update();
      gtk_widget_queue_draw(GTK_WIDGET(data));
    }
    return TRUE;
  }

  if (event->keyval == GDK_KEY_g) {
    pending_g = TRUE;
    gg_count = orig_count;
    return TRUE;
  }

  switch (event->keyval) {
  case GDK_KEY_q:
    pending_q = TRUE;
    return TRUE;
  case GDK_KEY_at:
    pending_at = TRUE;
    return TRUE;
  case GDK_KEY_plus:
  case GDK_KEY_equal:
    if (CELL_SIZE < 64) {
      CELL_SIZE += 2;
      zoom_resize();
    }
    break;
  case GDK_KEY_minus:
    if (CELL_SIZE > 4) {
      CELL_SIZE -= 2;
      zoom_resize();
    }
    break;
  case GDK_KEY_bar:
    show_grid = !show_grid;
    break;
  case GDK_KEY_percent:
    show_ruler = !show_ruler;
    break;
  case GDK_KEY_numbersign:
    show_checker = !show_checker;
    break;
  case GDK_KEY_i:
    insert_mode = TRUE;
    break;
  case GDK_KEY_H:
    cursor_x = MAX(cursor_x - 5 * n, 0);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_h:
  case GDK_KEY_Left:
    cursor_x = MAX(cursor_x - n, 0);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_L:
    cursor_x = MIN(cursor_x + 5 * n, CANVAS_W - 1);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_l:
  case GDK_KEY_Right:
    cursor_x = MIN(cursor_x + n, CANVAS_W - 1);
    cursor_x = snap_coord(cursor_x, FALSE);
    insert_paint();
    break;
  case GDK_KEY_K:
    cursor_y = MAX(cursor_y - 5 * n, 0);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_k:
  case GDK_KEY_Up:
    cursor_y = MAX(cursor_y - n, 0);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_J:
    cursor_y = MIN(cursor_y + 5 * n, CANVAS_H - 1);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_j:
  case GDK_KEY_Down:
    cursor_y = MIN(cursor_y + n, CANVAS_H - 1);
    cursor_y = snap_coord(cursor_y, TRUE);
    insert_paint();
    break;
  case GDK_KEY_G:
    cursor_y =
        orig_count > 0 ? CLAMP(orig_count - 1, 0, CANVAS_H - 1) : CANVAS_H - 1;
    break;
  case GDK_KEY_0:
    cursor_x = 0;
    break;
  case GDK_KEY_dollar:
    cursor_x = CANVAS_W - 1;
    break;
  case GDK_KEY_w:
    cursor_x = MIN(cursor_x + 5 * n, CANVAS_W - 1);
    break;
  case GDK_KEY_b:
    cursor_x = MAX(cursor_x - 5 * n, 0);
    break;
  case GDK_KEY_f:
    pending_f = TRUE;
    f_count = n;
    return TRUE;
  case GDK_KEY_n:
    if (last_find_dir == 1)
      find_right();
    else if (last_find_dir == -1)
      find_left();
    else if (last_find_dir == 2)
      find_down();
    else if (last_find_dir == -2)
      find_up();
    break;
  case GDK_KEY_N:
    if (last_find_dir == 1)
      find_left();
    else if (last_find_dir == -1)
      find_right();
    else if (last_find_dir == 2)
      find_up();
    else if (last_find_dir == -2)
      find_down();
    break;
  case GDK_KEY_d:
    pending_d = TRUE;
    d_count = n;
    return TRUE;
  case GDK_KEY_c: {
    /* Find the palette slot that matches fg_color, then cycle forward */
    if (PALETTE_SIZE <= 1) break;
    int cur_slot = -1;
    for (int i = 0; i < PALETTE_SIZE; i++) {
      int pr, pg, pb;
      palette_to_rgb(i, &pr, &pg, &pb);
      if (PACK_RGBA(pr, pg, pb, 255) == fg_color) {
        cur_slot = i;
        break;
      }
    }
    if (cur_slot < 1)
      cur_slot = 1;
    int next_slot = (cur_slot % (PALETTE_SIZE - 1)) + 1;
    int pr, pg, pb;
    palette_to_rgb(next_slot, &pr, &pg, &pb);
    fg_color = PACK_RGBA(pr, pg, pb, 255);
    gtk_widget_queue_draw(palette_bar);
    flash_color(next_slot);
    break;
  }
  case GDK_KEY_C: {
    if (PALETTE_SIZE <= 1) break;
    int cur_slot = -1;
    for (int i = 0; i < PALETTE_SIZE; i++) {
      int pr, pg, pb;
      palette_to_rgb(i, &pr, &pg, &pb);
      if (PACK_RGBA(pr, pg, pb, 255) == fg_color) {
        cur_slot = i;
        break;
      }
    }
    if (cur_slot < 1)
      cur_slot = 1;
    int prev_slot =
        (cur_slot - 2 + (PALETTE_SIZE - 1)) % (PALETTE_SIZE - 1) + 1;
    int pr, pg, pb;
    palette_to_rgb(prev_slot, &pr, &pg, &pb);
    fg_color = PACK_RGBA(pr, pg, pb, 255);
    gtk_widget_queue_draw(palette_bar);
    flash_color(prev_slot);
    break;
  }
  case GDK_KEY_e: {
    fg_color = PX(cursor_y, cursor_x); /* pick RGBA directly */
    gtk_widget_queue_draw(palette_bar);
    guchar er = (fg_color >> 24) & 0xff;
    guchar eg = (fg_color >> 16) & 0xff;
    guchar eb = (fg_color >> 8) & 0xff;
    char ebuf[32];
    snprintf(ebuf, sizeof(ebuf), "color #%02x%02x%02x", er, eg, eb);
    cmd_flash(ebuf);
    break;
  }
  case GDK_KEY_o: {
    int cx = cursor_x, cy = cursor_y, r = radius;
    begin_undo_action();
    if (r == 0) {
      paint_pixel(cx, cy, fg_color);
    } else {
      int bx = r, by = 0, err = 0;
#define CPSET(px, py)                                                          \
  do {                                                                         \
    paint_pixel((px), (py), fg_color);                                         \
  } while (0)
      while (bx >= by) {
        CPSET(cx + bx, cy + by);
        CPSET(cx + by, cy + bx);
        CPSET(cx - by, cy + bx);
        CPSET(cx - bx, cy + by);
        CPSET(cx - bx, cy - by);
        CPSET(cx - by, cy - bx);
        CPSET(cx + by, cy - bx);
        CPSET(cx + bx, cy - by);
        if (err <= 0) {
          by++;
          err += 2 * by + 1;
        }
        if (err > 0) {
          bx--;
          err -= 2 * bx + 1;
        }
      }
#undef CPSET
    }
    commit_undo_action();
    break;
  }
  case GDK_KEY_O: {
    int cx = cursor_x, cy = cursor_y, r = radius;
    begin_undo_action();
    for (int ey = cy - r; ey <= cy + r; ey++)
      for (int ex = cx - r; ex <= cx + r; ex++)
        if ((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy) <= r * r && ex >= 0 &&
            ex < CANVAS_W && ey >= 0 && ey < CANVAS_H)
          paint_pixel(ex, ey, fg_color);
    commit_undo_action();
    break;
  }
  case GDK_KEY_space:
  case GDK_KEY_r: {
    RECT_BOUNDS(radius);
    begin_undo_action();
    for (int ey = y0; ey <= y1; ey++)
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, ey, fg_color);
    commit_undo_action();
    last_action = GDK_KEY_r;
    last_radius = radius;
    last_was_visual = FALSE;
    break;
  }
  case GDK_KEY_R: {
    RECT_BOUNDS(radius);
    begin_undo_action();
    for (int ex = x0; ex <= x1; ex++) {
      paint_pixel(ex, y0, fg_color);
      paint_pixel(ex, y1, fg_color);
    }
    for (int ey = y0 + 1; ey < y1; ey++) {
      paint_pixel(x0, ey, fg_color);
      paint_pixel(x1, ey, fg_color);
    }
    commit_undo_action();
    last_action = GDK_KEY_R;
    last_radius = radius;
    last_was_visual = FALSE;
    break;
  }
  case GDK_KEY_D:
    begin_undo_action();
    for (int ex = cursor_x; ex < CANVAS_W; ex++)
      paint_pixel(ex, cursor_y, bg_color);
    commit_undo_action();
    last_action = GDK_KEY_x;
    last_was_visual = FALSE;
    break;
  case GDK_KEY_S:
    flood_fill(cursor_x, cursor_y, fg_color);
    break;
  case GDK_KEY_x: {
    RECT_BOUNDS(radius);
    begin_undo_action();
    for (int ey = y0; ey <= y1; ey++)
      for (int ex = x0; ex <= x1; ex++)
        paint_pixel(ex, ey, bg_color);
    commit_undo_action();
    last_action = GDK_KEY_x;
    last_radius = radius;
    last_was_visual = FALSE;
    break;
  }
  case GDK_KEY_period:
    if (last_was_visual) {
      int ax = cursor_x + last_visual_dx;
      int ay = cursor_y + last_visual_dy;
      begin_undo_action();
      if (last_action == GDK_KEY_backslash) {
        draw_line(ax, ay, cursor_x, cursor_y, fg_color);
      } else {
        guint32 val = (last_action == GDK_KEY_r) ? fg_color : bg_color;
        int x0 = CLAMP(MIN(cursor_x, ax), 0, CANVAS_W - 1);
        int x1 = CLAMP(MAX(cursor_x, ax), 0, CANVAS_W - 1);
        int y0 = CLAMP(MIN(cursor_y, ay), 0, CANVAS_H - 1);
        int y1 = CLAMP(MAX(cursor_y, ay), 0, CANVAS_H - 1);
        for (int ey = y0; ey <= y1; ey++)
          for (int ex = x0; ex <= x1; ex++)
            paint_pixel(ex, ey, val);
      }
      commit_undo_action();
    } else if (last_action == GDK_KEY_r || last_action == GDK_KEY_x ||
               last_action == GDK_KEY_R) {
      RECT_BOUNDS(last_radius);
      begin_undo_action();
      if (last_action == GDK_KEY_R) {
        for (int ex = x0; ex <= x1; ex++) {
          paint_pixel(ex, y0, fg_color);
          paint_pixel(ex, y1, fg_color);
        }
        for (int ey = y0 + 1; ey < y1; ey++) {
          paint_pixel(x0, ey, fg_color);
          paint_pixel(x1, ey, fg_color);
        }
      } else {
        guint32 val = (last_action == GDK_KEY_r) ? fg_color : bg_color;
        for (int ey = y0; ey <= y1; ey++)
          for (int ex = x0; ex <= x1; ex++)
            paint_pixel(ex, ey, val);
      }
      commit_undo_action();
    }
    break;
  case GDK_KEY_p:
    if (!yank_buf) {
      cmd_flash("Nothing yanked.");
    } else {
      begin_undo_action();
      for (int ey = 0; ey < yank_h; ey++)
        for (int ex = 0; ex < yank_w; ex++) {
          int px = cursor_x + ex, py = cursor_y + ey;
          if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
            push_undo(px, py);
            PX(py, px) = yank_buf[ey * yank_w + ex];
          }
        }
      commit_undo_action();
    }
    break;
  case GDK_KEY_P:
    if (!yank_buf) {
      cmd_flash("Nothing yanked.");
    } else {
      int ox = cursor_x - yank_w / 2;
      int oy = cursor_y - yank_h / 2;
      begin_undo_action();
      for (int ey = 0; ey < yank_h; ey++)
        for (int ex = 0; ex < yank_w; ex++) {
          int px = ox + ex, py = oy + ey;
          if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
            push_undo(px, py);
            PX(py, px) = yank_buf[ey * yank_w + ex];
          }
        }
      commit_undo_action();
    }
    break;
  case GDK_KEY_u:
    if (undo_count > 0) {
      undo_top--;
      undo_count--;
      int uidx = undo_top % undo_levels;
      UndoAction a = undo_stack[uidx];
      undo_stack[uidx] = (UndoAction){NULL, 0};
      int ridx = redo_top % undo_levels;
      if (redo_count == undo_levels)
        free_action(&redo_stack[ridx]);
      redo_stack[ridx] = a;
      redo_top++;
      if (redo_count < undo_levels)
        redo_count++;
      if (a.before_snap) {
        guint32 *np = malloc(a.before_w * a.before_h * sizeof(guint32));
        if (np) {
          memcpy(np, a.before_snap, a.before_w * a.before_h * sizeof(guint32));
          free(pixels);
          pixels = np;
          layer_bufs[layer_active] = pixels;
          CANVAS_W = a.before_w;
          CANVAS_H = a.before_h;
          cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
          cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
          zoom_resize();
        }
      } else {
        for (int i = a.count - 1; i >= 0; i--) {
          PX(a.changes[i].y, a.changes[i].x) = a.changes[i].before;
          cursor_x = a.changes[i].x;
          cursor_y = a.changes[i].y;
        }
      }
    }
    break;
  default:
    return FALSE;
  }

  status_update();
  gtk_widget_queue_draw(GTK_WIDGET(data));
  return TRUE;
}

static gboolean on_palette_click(GtkWidget *widget, GdkEventButton *event,
                                 gpointer data) {
  int idx = (int)(event->x / SWATCH_W);
  if (idx >= 0 && idx < PALETTE_SIZE) {
    int pr, pg, pb;
    palette_to_rgb(idx, &pr, &pg, &pb);
    fg_color = PACK_RGBA(pr, pg, pb, 255);
    gtk_widget_queue_draw(widget);
    flash_color(idx);
  }
  return TRUE;
}

static gboolean drag_painting = FALSE;
static gboolean drag_selecting = FALSE;
static guint32 drag_color = 0;

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
                                gpointer data) {
  if (event->button != 1 && event->button != 3)
    return FALSE;
  int cx = (int)(event->x / CELL_SIZE);
  int cy = (int)(event->y / CELL_SIZE);
  cursor_x = CLAMP(cx, 0, CANVAS_W - 1);
  cursor_y = CLAMP(cy, 0, CANVAS_H - 1);
  if (gradient_tool && event->button == 1) {
    grad_x0 = cursor_x;
    grad_y0 = cursor_y;
    grad_dragging = TRUE;
  } else if (insert_mode) {
    drag_color = (event->button == 3) ? bg_color : fg_color;
    begin_undo_action();
    drag_painting = TRUE;
    paint_brush(cursor_x, cursor_y, drag_color);
  } else if (event->button == 1) {
    visual_mode = TRUE;
    visual_anchor_x = cursor_x;
    visual_anchor_y = cursor_y;
    drag_selecting = TRUE;
  }
  status_update();
  gtk_widget_queue_draw(widget);
  return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event,
                                  gpointer data) {
  if (grad_dragging) {
    grad_dragging = FALSE;
    apply_gradient_linear(grad_x0, grad_y0, cursor_x, cursor_y,
                          grad_c1, grad_c2);
    gradient_tool = FALSE;
    visual_mode = FALSE;
    status_update();
    gtk_widget_queue_draw(widget);
    return TRUE;
  }
  if (drag_painting) {
    commit_undo_action();
    drag_painting = FALSE;
  }
  if (drag_selecting) {
    drag_selecting = FALSE;
    /* Collapse to a point click - cancel visual mode */
    if (cursor_x == visual_anchor_x && cursor_y == visual_anchor_y) {
      visual_mode = FALSE;
      status_update();
      gtk_widget_queue_draw(widget);
    }
  }
  return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                 gpointer data) {
  gboolean btn1 = event->state & GDK_BUTTON1_MASK;
  gboolean btn3 = event->state & GDK_BUTTON3_MASK;
  if (!btn1 && !btn3)
    return TRUE;
  int cx = CLAMP((int)(event->x / CELL_SIZE), 0, CANVAS_W - 1);
  int cy = CLAMP((int)(event->y / CELL_SIZE), 0, CANVAS_H - 1);
  if (cx == cursor_x && cy == cursor_y)
    return TRUE;
  cursor_x = cx;
  cursor_y = cy;
  if (insert_mode && drag_painting)
    paint_brush(cursor_x, cursor_y, drag_color);
  status_update();
  gtk_widget_queue_draw(widget); /* also redraws gradient preview */
  return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event,
                          gpointer data) {
  static double accum = 0;
  double dy = 0;
  if (event->direction == GDK_SCROLL_UP) {
    dy = -1;
  } else if (event->direction == GDK_SCROLL_DOWN) {
    dy = 1;
  } else if (event->direction == GDK_SCROLL_SMOOTH) {
    double dx;
    gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy);
    accum += dy;
    if (accum > -1.5 && accum < 1.5)
      return TRUE;
    dy = accum;
    accum = 0;
  }
  int delta = 2 * MAX(1, (int)(fabs(dy) / 1.5));
  if (dy < 0)
    CELL_SIZE = MIN(64, CELL_SIZE + delta);
  else if (dy > 0)
    CELL_SIZE = MAX(4, CELL_SIZE - delta);
  zoom_resize();
  return TRUE;
}

static void usage(const char *prog, int exitcode) {
  FILE *out = exitcode ? stderr : stdout;
  fprintf(out,
          "Usage: %s [OPTIONS] [FILE]\n"
          "\n"
          "A vim-inspired pixel art editor.\n"
          "\n"
          "Arguments:\n"
          "  FILE        PNG file to open at startup\n"
          "\n"
          "Options:\n"
          "  -W N        Canvas width in cells (default: %d)\n"
          "  -H N        Canvas height in cells (default: %d)\n"
          "  -z N        Cell size in pixels, 4-64 (default: %d)\n"
          "  -h          Show this help message\n",
          prog, CANVAS_W, CANVAS_H, CELL_SIZE);
  exit(exitcode);
}

int main(int argc, char *argv[]) {
  int explicit_w = 0, explicit_h = 0;
  int opt;
  while ((opt = getopt(argc, argv, "W:H:z:h")) != -1) {
    switch (opt) {
    case 'W': {
      int v = atoi(optarg);
      if (v > 0 && v <= 16384) {
        CANVAS_W = v;
        explicit_w = 1;
      } else if (v > 16384) {
        fprintf(stderr, "Warning: -W %d clamped to 16384\n", v);
        CANVAS_W = 16384;
        explicit_w = 1;
      }
      break;
    }
    case 'H': {
      int v = atoi(optarg);
      if (v > 0 && v <= 16384) {
        CANVAS_H = v;
        explicit_h = 1;
      } else if (v > 16384) {
        fprintf(stderr, "Warning: -H %d clamped to 16384\n", v);
        CANVAS_H = 16384;
        explicit_h = 1;
      }
      break;
    }
    case 'z': {
      int v = atoi(optarg);
      if (v > 0)
        CELL_SIZE = CLAMP(v, 4, 64);
      break;
    }
    case 'h':
      usage(argv[0], 0);
      break;
    default:
      usage(argv[0], 1);
    }
  }

  char **startup_files = (optind < argc) ? &argv[optind] : NULL;
  int startup_count = argc - optind;

  /* Size the initial canvas to match the first file if it is a valid PNG. */
  if (startup_count > 0) {
    cairo_surface_t *probe =
        cairo_image_surface_create_from_png(startup_files[0]);
    if (cairo_surface_status(probe) == CAIRO_STATUS_SUCCESS) {
      if (!explicit_w)
        CANVAS_W = cairo_image_surface_get_width(probe);
      if (!explicit_h)
        CANVAS_H = cairo_image_surface_get_height(probe);
    }
    cairo_surface_destroy(probe);
  }

  static const double default_pal[8][3] = {
      {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {0.8, 0.1, 0.1}, {0.1, 0.7, 0.1},
      {0.1, 0.3, 0.9}, {0.9, 0.8, 0.0}, {0.0, 0.7, 0.8}, {0.7, 0.0, 0.8},
  };
  palette_reserve(8);
  memcpy(palette, default_pal, sizeof(default_pal));
  palette_size = 8;

  pixels = calloc((size_t)CANVAS_W * CANVAS_H, sizeof(guint32));
  layer_bufs[0] = pixels;
  layer_visible[0] = TRUE;
  snprintf(layer_name[0], 32, "Layer 1");
  layer_count = 1;
  layer_active = 0;

  /* Pass only program name + positional args to gtk_init */
  char **gtk_argv = argv + optind - 1;
  gtk_argv[0] = argv[0];
  int gtk_argc = argc - optind + 1;
  gtk_init(&gtk_argc, &gtk_argv);

  main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWidget *window = main_window;
  gtk_window_set_title(GTK_WINDOW(window), "vim-paint");
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window), vbox);

  main_canvas = gtk_drawing_area_new();
  gtk_widget_set_size_request(main_canvas, CANVAS_W * CELL_SIZE,
                              CANVAS_H * CELL_SIZE);
  gtk_box_pack_start(GTK_BOX(vbox), main_canvas, FALSE, FALSE, 0);

  palette_bar = gtk_drawing_area_new();
  gtk_widget_set_size_request(palette_bar, CANVAS_W * CELL_SIZE, SWATCH_H);
  gtk_box_pack_start(GTK_BOX(vbox), palette_bar, FALSE, FALSE, 0);

  cmd_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(cmd_label), 0.0);
  gtk_widget_set_size_request(cmd_label, CANVAS_W * CELL_SIZE, 20);
  gtk_box_pack_start(GTK_BOX(vbox), cmd_label, FALSE, FALSE, 0);

  gtk_widget_add_events(main_canvas,
                        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                            GDK_BUTTON1_MOTION_MASK | GDK_BUTTON3_MOTION_MASK |
                            GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
  gtk_widget_add_events(palette_bar, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(main_canvas, "draw", G_CALLBACK(on_draw), NULL);
  g_signal_connect(main_canvas, "button-press-event",
                   G_CALLBACK(on_button_press), NULL);
  g_signal_connect(main_canvas, "button-release-event",
                   G_CALLBACK(on_button_release), NULL);
  g_signal_connect(main_canvas, "motion-notify-event",
                   G_CALLBACK(on_motion_notify), NULL);
  g_signal_connect(main_canvas, "scroll-event", G_CALLBACK(on_scroll), NULL);
  g_signal_connect(palette_bar, "draw", G_CALLBACK(on_palette_draw), NULL);
  g_signal_connect(palette_bar, "button-press-event",
                   G_CALLBACK(on_palette_click), NULL);
  g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press),
                   main_canvas);

  gtk_widget_show_all(window);
  status_update();

  if (startup_count > 0) {
    cmd_open(startup_files[0]);
    for (int i = 1; i < startup_count && tab_count < TAB_MAX; i++) {
      tab_save(tab_current);
      guint32 *np =
          calloc(DEFAULT_CANVAS_W * DEFAULT_CANVAS_H, sizeof(guint32));
      if (!np)
        break;
      free(pixels);
      pixels = np;
      CANVAS_W = DEFAULT_CANVAS_W;
      CANVAS_H = DEFAULT_CANVAS_H;
      cursor_x = 0;
      cursor_y = 0;
      canvas_dirty = FALSE;
      visual_mode = FALSE;
      insert_mode = FALSE;
      last_filename[0] = '\0';
      tab_count++;
      tab_current = tab_count - 1;
      tab_save(tab_current);
      cmd_open(startup_files[i]);
    }
    if (tab_count > 1)
      tab_switch(0);
  }

  gtk_main();

  return 0;
}
