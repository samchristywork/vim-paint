#include "state.h"

int CANVAS_W = DEFAULT_CANVAS_W;
int CANVAS_H = DEFAULT_CANVAS_H;
int CELL_SIZE = 12;

double (*palette)[3] = NULL;
int palette_size = 0;
int palette_cap = 0;

guint32 *pixels = NULL;
guint32 fg_color = 0x000000ff;
guint32 bg_color = 0xffffffff;

int cursor_x = 0;
int cursor_y = 0;
gboolean visual_mode = FALSE;
int visual_anchor_x = 0;
int visual_anchor_y = 0;
gboolean insert_mode = FALSE;
gboolean show_grid = TRUE;
guint32 grid_color = 0xccccccff;
gboolean show_ruler = FALSE;
gboolean show_checker = FALSE;
gboolean show_onionskin = FALSE;
int onionskin_opacity = 50;
gboolean gradient_tool = FALSE;
gboolean grad_dragging = FALSE;
int grad_x0 = 0, grad_y0 = 0;
guint32 grad_c1 = 0, grad_c2 = 0;

Guide guides[GUIDE_MAX];
int guide_count = 0;
gboolean guide_snap = FALSE;
gboolean canvas_dirty = FALSE;
SymMode sym_mode = SYM_NONE;
int sym_radial_n = 4;
int brush_size = 1;
int brush_shape = 0;
int custom_brush_w = 0, custom_brush_h = 0;
gboolean custom_brush_pixels[16][16];
int spray_density = 0;
int ellipse_ry = 0;

FillPattern fill_pattern = FILL_SOLID;
char text_font_family[256] = "Monospace";
double text_font_size = 10.0;

int undo_levels = 64;
PixelChange *staged = NULL;
int staged_count = 0, staged_cap = 0;
UndoAction undo_stack[UNDO_MAX];
int undo_top = 0, undo_count = 0;
UndoAction redo_stack[UNDO_MAX];
int redo_top = 0, redo_count = 0;

guint32 *layer_bufs[LAYER_MAX];
gboolean layer_visible[LAYER_MAX];
char layer_name[LAYER_MAX][32];
int layer_opacity[LAYER_MAX];
int layer_count = 1;
int layer_active = 0;
BlendMode layer_blend[LAYER_MAX];

const char *blend_mode_names[] = {"normal",      "multiply",   "screen",
                                  "overlay",     "darken",     "lighten",
                                  "color-dodge", "color-burn", "hard-light",
                                  "soft-light",  "difference", "exclusion"};

TabState tabs[TAB_MAX];
int tab_count = 1;
int tab_current = 0;

gboolean cmd_mode = FALSE;
char cmd_buf[4096];
int cmd_len = 0;

MacroEvent macro_buf[26][MACRO_MAX_EVENTS];
int macro_len[26];
gboolean macro_recording = FALSE;
int macro_reg = -1;
gboolean macro_playing = FALSE;

char cmd_history[CMD_HISTORY_MAX][4096];
int cmd_history_count = 0;
int cmd_history_idx = -1;
char cmd_history_draft[4096] = "";

glob_t tab_glob;
gboolean tab_glob_valid = FALSE;
int tab_glob_idx = 0;
char tab_cmd_prefix[32];

int color_tab_matches[256];
int color_tab_count = 0;
int color_tab_idx = 0;
gboolean color_tab_valid = FALSE;
char color_tab_prefix[64];
char last_filename[4096] = "";
GtkWidget *cmd_label = NULL;
GtkWidget *main_canvas = NULL;
GtkWidget *palette_bar = NULL;
GtkWidget *main_window = NULL;

guint flash_timer_id = 0;

guint32 *yank_buf = NULL;
int yank_w = 0;
int yank_h = 0;
