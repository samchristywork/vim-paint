#pragma once

#include <glob.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_CANVAS_W 80
#define DEFAULT_CANVAS_H 40

#define PACK_RGBA(r, g, b, a)                                                  \
  (((guint32)(r) << 24) | ((guint32)(g) << 16) | ((guint32)(b) << 8) |         \
   (guint32)(a))

#define PX(y, x) pixels[(y) * CANVAS_W + (x)]
#define RECT_BOUNDS(rad)                                                       \
  int x0 = CLAMP(cursor_x - (rad), 0, CANVAS_W - 1);                           \
  int x1 = CLAMP(cursor_x + (rad), 0, CANVAS_W - 1);                           \
  int y0 = CLAMP(cursor_y - (rad), 0, CANVAS_H - 1);                           \
  int y1 = CLAMP(cursor_y + (rad), 0, CANVAS_H - 1)

#define PALETTE_SIZE palette_size
#define UNDO_MAX 256
#define LAYER_MAX 8
#define TAB_MAX 8
#define GUIDE_MAX 64
#define SWATCH_W 24
#define SWATCH_H 20
#define MACRO_MAX_EVENTS 4096
#define CMD_HISTORY_MAX 64
extern int named_colors_count;
#define NAMED_COLORS_COUNT named_colors_count

typedef struct {
  int coord;
  gboolean horizontal;
} Guide;

typedef enum { SYM_NONE = 0, SYM_H, SYM_V, SYM_HV, SYM_RADIAL } SymMode;

typedef enum {
  FILL_SOLID = 0,
  FILL_CHECKER,
  FILL_HSTRIPES,
  FILL_VSTRIPES,
  FILL_HALFTONE
} FillPattern;

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

typedef struct {
  guint keyval;
  GdkModifierType state;
} MacroEvent;

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

extern int CANVAS_W;
extern int CANVAS_H;
extern int CELL_SIZE;

extern double (*palette)[3];
extern int palette_size;
extern int palette_cap;

extern guint32 *pixels;
extern guint32 fg_color;
extern guint32 bg_color;

extern int cursor_x;
extern int cursor_y;
extern gboolean visual_mode;
extern int visual_anchor_x;
extern int visual_anchor_y;
extern gboolean insert_mode;
extern gboolean show_grid;
extern guint32 grid_color;
extern gboolean show_ruler;
extern gboolean show_checker;
extern gboolean show_onionskin;
extern int onionskin_opacity;
extern gboolean gradient_tool;
extern gboolean grad_dragging;
extern int grad_x0, grad_y0;
extern guint32 grad_c1, grad_c2;

extern Guide guides[GUIDE_MAX];
extern int guide_count;
extern gboolean guide_snap;
extern gboolean canvas_dirty;
extern SymMode sym_mode;
extern int sym_radial_n;
extern int brush_size;
extern int brush_shape;
extern int custom_brush_w, custom_brush_h;
extern gboolean custom_brush_pixels[16][16];
extern int spray_density;
extern int ellipse_ry;

extern FillPattern fill_pattern;
extern char text_font_family[256];
extern double text_font_size;

extern int undo_levels;
extern PixelChange *staged;
extern int staged_count, staged_cap;
extern UndoAction undo_stack[UNDO_MAX];
extern int undo_top, undo_count;
extern UndoAction redo_stack[UNDO_MAX];
extern int redo_top, redo_count;

extern guint32 *layer_bufs[LAYER_MAX];
extern gboolean layer_visible[LAYER_MAX];
extern char layer_name[LAYER_MAX][32];
extern int layer_opacity[LAYER_MAX];
extern int layer_count;
extern int layer_active;
extern BlendMode layer_blend[LAYER_MAX];
extern const char *blend_mode_names[];

extern TabState tabs[TAB_MAX];
extern int tab_count;
extern int tab_current;

extern gboolean cmd_mode;
extern char cmd_buf[4096];
extern int cmd_len;

extern MacroEvent macro_buf[26][MACRO_MAX_EVENTS];
extern int macro_len[26];
extern gboolean macro_recording;
extern int macro_reg;
extern gboolean macro_playing;

extern char cmd_history[CMD_HISTORY_MAX][4096];
extern int cmd_history_count;
extern int cmd_history_idx;
extern char cmd_history_draft[4096];

extern glob_t tab_glob;
extern gboolean tab_glob_valid;
extern int tab_glob_idx;
extern char tab_cmd_prefix[32];

extern int color_tab_matches[256];
extern int color_tab_count;
extern int color_tab_idx;
extern gboolean color_tab_valid;
extern char color_tab_prefix[64];
extern char last_filename[4096];
extern GtkWidget *cmd_label;
extern GtkWidget *main_canvas;
extern GtkWidget *palette_bar;
extern GtkWidget *main_window;

extern guint flash_timer_id;

extern guint32 *yank_buf;
extern int yank_w;
extern int yank_h;

typedef struct { const char *name; unsigned int rgb; } NamedColor;
extern const NamedColor named_colors[];

/* palette.c */
int palette_reserve(int needed);
void palette_to_rgb(int idx, int *r, int *g, int *b);
void set_palette_rgb(int slot, unsigned int rgb);
void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l);
double hue_to_rgb(double p, double q, double t);
void hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b);
gboolean parse_color(const char *val, unsigned int *out_rgb);

/* layers.c */
double blend_apply(BlendMode mode, double cb, double cs);
void layers_composite(guint32 *dst, int total);
void layers_flatten(void);

/* undo.c */
void free_action(UndoAction *a);
void clear_history(void);
void begin_undo_action(void);
void push_undo(int x, int y);
void commit_undo_action(void);
void commit_canvas_snapshot(guint32 *before_snap, int bw, int bh);

/* canvas.c */
void insert_paint(void);
void draw_line(int x0, int y0, int x1, int y1, guint32 color);
void paint_pixel_raw(int x, int y, guint32 color);
void paint_pixel(int x, int y, guint32 color);
void paint_brush(int x, int y, guint32 color);
void find_right(void);
void find_left(void);
void find_down(void);
void find_up(void);
guint32 fill_color_at(int x, int y, guint32 fg);
void flood_fill(int sx, int sy, guint32 fill_color);
int snap_coord(int coord, gboolean horizontal);
void apply_gradient_linear(int x0, int y0, int x1, int y1, guint32 c1, guint32 c2);

/* fileio.c */
gboolean cmd_write(const char *filename);
void cmd_export_bmp(const char *filename, int scale);
void cmd_open(const char *filename);

/* commands.c */
void cmd_execute(void);

/* render.c */
gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_palette_draw(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean on_palette_click(GtkWidget *widget, GdkEventButton *event, gpointer data);

/* input.c */
gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data);
gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data);
gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data);
gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data);

/* main.c */
void zoom_resize(void);
void tab_reset(void);
void flash_color(int idx);
void update_title(const char *filename);
void title_refresh(void);
void tab_save(int idx);
void tab_switch(int newidx);
void tab_close_current(void);
void cmd_set(const char *text);
void status_update(void);
gboolean on_flash_expire(gpointer data);
void cmd_flash(const char *text);
void usage(const char *prog, int exitcode);
int main(int argc, char *argv[]);
