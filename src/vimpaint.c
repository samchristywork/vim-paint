#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

static int CANVAS_W = 80;
static int CANVAS_H = 40;
static int CELL_SIZE = 12;

/* palette: index 0 = background (white), 1..N = foreground colors */
#define PALETTE_MAX 256
static double palette[PALETTE_MAX][3] = {
    {1.0, 1.0, 1.0},  /* 0 white  (background) */
    {0.0, 0.0, 0.0},  /* 1 black  */
    {0.8, 0.1, 0.1},  /* 2 red    */
    {0.1, 0.7, 0.1},  /* 3 green  */
    {0.1, 0.3, 0.9},  /* 4 blue   */
    {0.9, 0.8, 0.0},  /* 5 yellow */
    {0.0, 0.7, 0.8},  /* 6 cyan   */
    {0.7, 0.0, 0.8},  /* 7 magenta*/
};
static int palette_size = 8;
#define PALETTE_SIZE palette_size

static guchar *pixels = NULL;  /* flat [y * CANVAS_W + x], 0=background 1..7=palette */
static guchar fg_color = 1;   /* current foreground color index */

#define PX(y, x) pixels[(y) * CANVAS_W + (x)]
static int cursor_x = 0;
static int cursor_y = 0;
static gboolean visual_mode = FALSE;
static int visual_anchor_x = 0;
static int visual_anchor_y = 0;
static gboolean insert_mode = FALSE;
static gboolean show_grid = TRUE;

static gboolean cmd_mode = FALSE;
static char cmd_buf[256];
static int cmd_len = 0;
static char last_filename[256] = "";
static GtkWidget *cmd_label = NULL;
static GtkWidget *main_canvas = NULL;
static GtkWidget *palette_bar = NULL;
static GtkWidget *main_window = NULL;

#define SWATCH_W 24
#define SWATCH_H 20

static void zoom_resize(void) {
    gtk_widget_set_size_request(main_canvas, CANVAS_W * CELL_SIZE, CANVAS_H * CELL_SIZE);
    gtk_widget_set_size_request(palette_bar, CANVAS_W * CELL_SIZE, SWATCH_H);
    gtk_widget_set_size_request(cmd_label,   CANVAS_W * CELL_SIZE, 20);
    gtk_window_resize(GTK_WINDOW(main_window), 1, 1);
}

static void cmd_flash(const char *text);
static void clear_history(void);

static void flash_color(int idx) {
    char buf[64];
    int r = (int)(palette[idx][0] * 255 + 0.5);
    int g = (int)(palette[idx][1] * 255 + 0.5);
    int b = (int)(palette[idx][2] * 255 + 0.5);
    snprintf(buf, sizeof(buf), "color %d  #%02x%02x%02x", idx, r, g, b);
    cmd_flash(buf);
}

static void update_title(const char *filename) {
    char buf[300];
    snprintf(buf, sizeof(buf), "vim-paint - %s", filename);
    gtk_window_set_title(GTK_WINDOW(main_window), buf);
}

static gboolean on_palette_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    for (int i = 0; i < PALETTE_SIZE; i++) {
        cairo_set_source_rgb(cr, palette[i][0], palette[i][1], palette[i][2]);
        cairo_rectangle(cr, i * SWATCH_W, 0, SWATCH_W, SWATCH_H);
        cairo_fill(cr);
        if (i == fg_color) {
            cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
            cairo_set_line_width(cr, 2.0);
            cairo_rectangle(cr, i * SWATCH_W + 1, 1, SWATCH_W - 2, SWATCH_H - 2);
            cairo_stroke(cr);
        }
    }
    /* Fill remaining space with a neutral background */
    cairo_set_source_rgb(cr, 0.85, 0.85, 0.85);
    cairo_rectangle(cr, PALETTE_SIZE * SWATCH_W, 0, CANVAS_W * CELL_SIZE, SWATCH_H);
    cairo_fill(cr);
    return FALSE;
}

static void cmd_set(const char *text) {
    gtk_label_set_text(GTK_LABEL(cmd_label), text);
}

static guint flash_timer_id = 0;

static void status_update(void) {
    if (cmd_mode) return;
    if (flash_timer_id) return;
    char buf[64];
    const char *mode = visual_mode ? "VISUAL" : insert_mode ? "INSERT" : "NORMAL";
    snprintf(buf, sizeof(buf), " %s  col: %d  row: %d", mode, cursor_x + 1, cursor_y + 1);
    gtk_label_set_text(GTK_LABEL(cmd_label), buf);
}

static gboolean on_flash_expire(gpointer data) {
    flash_timer_id = 0;
    status_update();
    return G_SOURCE_REMOVE;
}

static void cmd_flash(const char *text) {
    cmd_set(text);
    if (flash_timer_id) g_source_remove(flash_timer_id);
    flash_timer_id = g_timeout_add(2000, on_flash_expire, NULL);
}

static gboolean cmd_write(const char *filename) {
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24,
                                                       CANVAS_W, CANVAS_H);
    guchar *d = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < CANVAS_H; y++)
        for (int x = 0; x < CANVAS_W; x++) {
            int idx = PX(y, x);
            d[y * stride + x * 4 + 0] = (guchar)(palette[idx][2] * 255);
            d[y * stride + x * 4 + 1] = (guchar)(palette[idx][1] * 255);
            d[y * stride + x * 4 + 2] = (guchar)(palette[idx][0] * 255);
        }
    cairo_surface_mark_dirty(surf);
    gboolean ok = cairo_surface_write_to_png(surf, filename) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surf);
    if (ok)
        cmd_flash("Written.");
    else
        cmd_flash("Write failed.");
    return ok;
}

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

static void cmd_execute(void) {
    /* Extract argument: text after the command verb, leading spaces stripped */
    const char *p = cmd_buf + 1;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    const char *arg = p;

    if (strcmp(cmd_buf, ":q") == 0) {
        gtk_main_quit();
        return;
    }

    if (strcmp(cmd_buf, ":new") == 0) {
        memset(pixels, 0, CANVAS_W * CANVAS_H);
        clear_history();
        last_filename[0] = '\0';
        cursor_x = 0; cursor_y = 0;
        gtk_window_set_title(GTK_WINDOW(main_window), "vim-paint");
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
        return;
    }

    if (strcmp(cmd_buf, ":w") == 0) {
        if (*last_filename) cmd_write(last_filename);
        else cmd_flash("No filename. Use :w filename");
        return;
    }

    if (strcmp(cmd_buf, ":wq") == 0) {
        if (*last_filename) {
            if (cmd_write(last_filename)) gtk_main_quit();
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
        if (*last_filename) {
            snprintf(cmd_buf, sizeof(cmd_buf), ":e %s", last_filename);
            cmd_len = strlen(cmd_buf);
            cmd_execute();
        } else {
            cmd_flash("No filename.");
        }
        return;
    }

    if (strncmp(cmd_buf, ":e ", 3) == 0 && *arg) {
        cairo_surface_t *surf = cairo_image_surface_create_from_png(arg);
        if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
            cmd_flash("Open failed.");
            cairo_surface_destroy(surf);
            return;
        }
        guchar *d = cairo_image_surface_get_data(surf);
        int stride = cairo_image_surface_get_stride(surf);
        int w = MIN(cairo_image_surface_get_width(surf), CANVAS_W);
        int h = MIN(cairo_image_surface_get_height(surf), CANVAS_H);
        memset(pixels, 0, CANVAS_W * CANVAS_H);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                double pr = d[y * stride + x * 4 + 2] / 255.0;
                double pg = d[y * stride + x * 4 + 1] / 255.0;
                double pb = d[y * stride + x * 4 + 0] / 255.0;
                int best = 0;
                double best_dist = 1e9;
                for (int i = 0; i < PALETTE_SIZE; i++) {
                    double dr = pr - palette[i][0];
                    double dg = pg - palette[i][1];
                    double db = pb - palette[i][2];
                    double dist = dr*dr + dg*dg + db*db;
                    if (dist < best_dist) { best_dist = dist; best = i; }
                }
                PX(y, x) = best;
            }
        cairo_surface_destroy(surf);
        clear_history();
        snprintf(last_filename, sizeof(last_filename), "%s", arg);
        update_title(last_filename);
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
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
        } else if (strncmp(opt, "color ", 6) == 0) {
            const char *val = opt + 6;
            unsigned int rgb = 0;
            gboolean have_rgb = FALSE;
            if (val[0] == '#' && strlen(val) == 7) {
                have_rgb = sscanf(val + 1, "%06x", &rgb) == 1;
                if (!have_rgb) cmd_flash("Invalid hex color.");
            } else {
                for (int i = 0; i < NAMED_COLORS_COUNT; i++) {
                    if (strcmp(val, named_colors[i].name) == 0) {
                        rgb = named_colors[i].rgb; have_rgb = TRUE; break;
                    }
                }
                if (!have_rgb) {
                    int n = atoi(val);
                    if (n > 0 && n < PALETTE_SIZE) {
                        fg_color = (guchar)n;
                        gtk_widget_queue_draw(palette_bar);
                        flash_color(fg_color);
                    } else {
                        cmd_flash("Unknown color.");
                    }
                }
            }
            if (have_rgb) {
                double pr = ((rgb >> 16) & 0xff) / 255.0;
                double pg = ((rgb >>  8) & 0xff) / 255.0;
                double pb = ( rgb        & 0xff) / 255.0;
                int found = -1;
                for (int i = 0; i < PALETTE_SIZE; i++) {
                    if (palette[i][0] == pr && palette[i][1] == pg && palette[i][2] == pb) {
                        found = i; break;
                    }
                }
                if (found < 0) {
                    if (PALETTE_SIZE >= PALETTE_MAX) {
                        cmd_flash("Palette full.");
                    } else {
                        found = palette_size;
                        palette[found][0] = pr;
                        palette[found][1] = pg;
                        palette[found][2] = pb;
                        palette_size++;
                        fg_color = (guchar)found;
                        gtk_widget_queue_draw(palette_bar);
                        flash_color(fg_color);
                    }
                } else {
                    fg_color = (guchar)found;
                    gtk_widget_queue_draw(palette_bar);
                    flash_color(fg_color);
                }
            }
        } else {
            cmd_flash("Unknown option.");
        }
        return;
    }

    if (strncmp(cmd_buf, ":resize ", 8) == 0 && *arg) {
        int nw = 0, nh = 0;
        if (sscanf(arg, "%dx%d", &nw, &nh) != 2)
            sscanf(arg, "%d %d", &nw, &nh);
        if (nw < 1 || nh < 1) { cmd_flash("Usage: :resize WxH"); return; }
        guchar *np = calloc(nw * nh, 1);
        if (!np) { cmd_flash("Out of memory."); return; }
        int cw = MIN(CANVAS_W, nw), ch = MIN(CANVAS_H, nh);
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++)
                np[y * nw + x] = PX(y, x);
        free(pixels);
        pixels = np;
        CANVAS_W = nw;
        CANVAS_H = nh;
        cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
        cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
        clear_history();
        zoom_resize();
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
        return;
    }

    if (strncmp(cmd_buf, ":savep ", 7) == 0 && *arg) {
        FILE *f = fopen(arg, "w");
        if (!f) { cmd_flash("Cannot open file."); return; }
        for (int i = 0; i < PALETTE_SIZE; i++) {
            int r = (int)(palette[i][0] * 255 + 0.5);
            int g = (int)(palette[i][1] * 255 + 0.5);
            int b = (int)(palette[i][2] * 255 + 0.5);
            fprintf(f, "#%02x%02x%02x\n", r, g, b);
        }
        fclose(f);
        cmd_flash("Palette saved.");
        return;
    }

    if (strcmp(cmd_buf, ":fliph") == 0) {
        for (int y = 0; y < CANVAS_H; y++)
            for (int x = 0; x < CANVAS_W / 2; x++) {
                guchar tmp = PX(y, x);
                PX(y, x) = PX(y, CANVAS_W - 1 - x);
                PX(y, CANVAS_W - 1 - x) = tmp;
            }
        clear_history();
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
        return;
    }

    if (strcmp(cmd_buf, ":flipv") == 0) {
        for (int y = 0; y < CANVAS_H / 2; y++)
            for (int x = 0; x < CANVAS_W; x++) {
                guchar tmp = PX(y, x);
                PX(y, x) = PX(CANVAS_H - 1 - y, x);
                PX(CANVAS_H - 1 - y, x) = tmp;
            }
        clear_history();
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
        return;
    }

    if (strcmp(cmd_buf, ":rotate") == 0) {
        int nW = CANVAS_H, nH = CANVAS_W;
        guchar *np = malloc(nW * nH);
        if (!np) { cmd_flash("Out of memory."); return; }
        for (int y = 0; y < CANVAS_H; y++)
            for (int x = 0; x < CANVAS_W; x++)
                np[x * nW + (CANVAS_H - 1 - y)] = PX(y, x);
        free(pixels);
        pixels = np;
        CANVAS_W = nW;
        CANVAS_H = nH;
        cursor_x = CLAMP(cursor_x, 0, CANVAS_W - 1);
        cursor_y = CLAMP(cursor_y, 0, CANVAS_H - 1);
        clear_history();
        zoom_resize();
        gtk_widget_queue_draw(main_canvas);
        cmd_set("");
        return;
    }

    cmd_flash("Unknown command.");
}

static guchar *yank_buf = NULL;
static int yank_w = 0;
static int yank_h = 0;

#define UNDO_MAX 64
typedef struct { int x, y; guchar before, after; } PixelChange;
typedef struct { PixelChange *changes; int count; } UndoAction;

static UndoAction undo_stack[UNDO_MAX];
static int undo_top = 0, undo_count = 0;
static UndoAction redo_stack[UNDO_MAX];
static int redo_top = 0, redo_count = 0;

static PixelChange *staged = NULL;
static int staged_count = 0, staged_cap = 0;

static void free_action(UndoAction *a) {
    free(a->changes);
    a->changes = NULL;
    a->count = 0;
}

static void clear_history(void) {
    for (int i = 0; i < undo_count; i++)
        free_action(&undo_stack[(undo_top - undo_count + i + UNDO_MAX * 2) % UNDO_MAX]);
    undo_top = 0; undo_count = 0;
    for (int i = 0; i < redo_count; i++)
        free_action(&redo_stack[(redo_top - redo_count + i + UNDO_MAX * 2) % UNDO_MAX]);
    redo_top = 0; redo_count = 0;
    staged_count = 0;
}

static void begin_undo_action(void) {
    staged_count = 0;
    for (int i = 0; i < redo_count; i++)
        free_action(&redo_stack[(redo_top - redo_count + i + UNDO_MAX * 2) % UNDO_MAX]);
    redo_top = 0;
    redo_count = 0;
}

static void push_undo(int x, int y) {
    if (staged_count >= staged_cap) {
        staged_cap = staged_cap ? staged_cap * 2 : 16;
        staged = realloc(staged, staged_cap * sizeof(PixelChange));
    }
    staged[staged_count++] = (PixelChange){x, y, PX(y, x), 0};
}

static void commit_undo_action(void) {
    if (staged_count == 0) return;
    for (int i = 0; i < staged_count; i++)
        staged[i].after = PX(staged[i].y, staged[i].x);
    int idx = undo_top % UNDO_MAX;
    if (undo_count == UNDO_MAX) free_action(&undo_stack[idx]);
    PixelChange *copy = malloc(staged_count * sizeof(PixelChange));
    memcpy(copy, staged, staged_count * sizeof(PixelChange));
    undo_stack[idx] = (UndoAction){copy, staged_count};
    undo_top++;
    if (undo_count < UNDO_MAX) undo_count++;
    staged_count = 0;
}

static void find_right(void) {
    for (int fx = cursor_x + 1; fx < CANVAS_W; fx++)
        if (PX(cursor_y, fx)) { cursor_x = fx; return; }
    for (int fx = 0; fx < cursor_x; fx++)
        if (PX(cursor_y, fx)) { cursor_x = fx; return; }
}
static void find_left(void) {
    for (int fx = cursor_x - 1; fx >= 0; fx--)
        if (PX(cursor_y, fx)) { cursor_x = fx; return; }
    for (int fx = CANVAS_W - 1; fx > cursor_x; fx--)
        if (PX(cursor_y, fx)) { cursor_x = fx; return; }
}
static void find_down(void) {
    for (int fy = cursor_y + 1; fy < CANVAS_H; fy++)
        if (PX(fy, cursor_x)) { cursor_y = fy; return; }
    for (int fy = 0; fy < cursor_y; fy++)
        if (PX(fy, cursor_x)) { cursor_y = fy; return; }
}
static void find_up(void) {
    for (int fy = cursor_y - 1; fy >= 0; fy--)
        if (PX(fy, cursor_x)) { cursor_y = fy; return; }
    for (int fy = CANVAS_H - 1; fy > cursor_y; fy--)
        if (PX(fy, cursor_x)) { cursor_y = fy; return; }
}

static void flood_fill(int sx, int sy, guchar fill_color) {
    guchar target = PX(sy, sx);
    if (target == fill_color) return;
    int total = CANVAS_W * CANVAS_H;
    int *queue = malloc(total * sizeof(int));
    if (!queue) return;
    begin_undo_action();
    int head = 0, tail = 0;
    queue[tail++] = sy * CANVAS_W + sx;
    push_undo(sx, sy);
    PX(sy, sx) = fill_color;
    while (head < tail) {
        int pos = queue[head++];
        int x = pos % CANVAS_W, y = pos / CANVAS_W;
        int neighbors[4][2] = {{x-1,y},{x+1,y},{x,y-1},{x,y+1}};
        for (int i = 0; i < 4; i++) {
            int nx = neighbors[i][0], ny = neighbors[i][1];
            if (nx < 0 || nx >= CANVAS_W || ny < 0 || ny >= CANVAS_H) continue;
            if (PX(ny, nx) != target) continue;
            push_undo(nx, ny);
            PX(ny, nx) = fill_color;
            queue[tail++] = ny * CANVAS_W + nx;
        }
    }
    free(queue);
    commit_undo_action();
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            int idx = PX(y, x);
            cairo_set_source_rgb(cr, palette[idx][0], palette[idx][1], palette[idx][2]);
            cairo_rectangle(cr, x * CELL_SIZE, y * CELL_SIZE, CELL_SIZE, CELL_SIZE);
            cairo_fill(cr);
        }
    }

    /* Draw grid lines */
    if (show_grid) {
        cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 1.0);
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

    /* Draw cursor: filled green in insert mode, outlined red/white in normal mode */
    if (insert_mode) {
        cairo_set_source_rgba(cr, 0.0, 0.85, 0.3, 0.55);
        cairo_rectangle(cr, cursor_x * CELL_SIZE, cursor_y * CELL_SIZE,
                        CELL_SIZE, CELL_SIZE);
        cairo_fill(cr);
    } else {
        if (PX(cursor_y, cursor_x)) {
            cairo_set_source_rgb(cr, 1, 1, 1);
        } else {
            cairo_set_source_rgb(cr, 1, 0, 0);
        }
        double lw = CLAMP(CELL_SIZE * 0.2, 1.0, 2.0);
        int inset = MAX(1, CELL_SIZE / 8);
        cairo_set_line_width(cr, lw);
        cairo_rectangle(cr, cursor_x * CELL_SIZE + inset, cursor_y * CELL_SIZE + inset,
                        CELL_SIZE - 2 * inset, CELL_SIZE - 2 * inset);
        cairo_stroke(cr);
    }

    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    if (cmd_mode) {
        if (event->keyval == GDK_KEY_Escape) {
            cmd_mode = FALSE;
            status_update();
        } else if (event->keyval == GDK_KEY_Return) {
            cmd_mode = FALSE;
            cmd_execute();
        } else if (event->keyval == GDK_KEY_BackSpace) {
            if (cmd_len > 1) { cmd_buf[--cmd_len] = '\0'; cmd_set(cmd_buf); }
        } else {
            guint32 uc = gdk_keyval_to_unicode(event->keyval);
            if (uc >= 0x20 && uc < 0x7f && cmd_len < 254) {
                cmd_buf[cmd_len++] = (char)uc;
                cmd_buf[cmd_len] = '\0';
                cmd_set(cmd_buf);
            }
        }
        return TRUE;
    }

    if (event->is_modifier) return FALSE;

    static gboolean pending_g = FALSE;
    static gboolean pending_d = FALSE;
    static gboolean pending_f = FALSE;
    static int d_count = 1;
    static int f_count = 1;

    if (event->keyval == GDK_KEY_colon) {
        pending_g = FALSE;
        pending_d = FALSE;
        pending_f = FALSE;
        cmd_mode = TRUE;
        cmd_buf[0] = ':'; cmd_buf[1] = '\0'; cmd_len = 1;
        cmd_set(cmd_buf);
        return TRUE;
    }
    static int count = 0;
    static guint last_action = 0;
    static int last_radius = 0;
    static int last_find_dir = 0;  /* +1 = forward (f), -1 = backward (F), 0 = none */
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
    }

    if (pending_d) {
        pending_d = FALSE;
        int x0 = cursor_x, x1 = cursor_x;
        int y0 = cursor_y, y1 = cursor_y;
        gboolean whole_row = FALSE;
        switch (event->keyval) {
        case GDK_KEY_d: whole_row = TRUE; break;
        case GDK_KEY_h: x0 = MAX(cursor_x - d_count, 0); break;
        case GDK_KEY_l: x1 = MIN(cursor_x + d_count, CANVAS_W - 1); break;
        case GDK_KEY_w: x1 = MIN(cursor_x + 5 * d_count, CANVAS_W - 1); break;
        case GDK_KEY_b: x0 = MAX(cursor_x - 5 * d_count, 0); break;
        case GDK_KEY_0: x0 = 0; break;
        case GDK_KEY_dollar: x1 = CANVAS_W - 1; break;
        case GDK_KEY_j: whole_row = TRUE; y1 = MIN(cursor_y + d_count, CANVAS_H - 1); break;
        case GDK_KEY_k: whole_row = TRUE; y0 = MAX(cursor_y - d_count, 0); break;
        default: return TRUE;
        }
        begin_undo_action();
        if (whole_row) {
            for (int ey = y0; ey <= y1; ey++)
                for (int ex = 0; ex < CANVAS_W; ex++) {
                    push_undo(ex, ey);
                    PX(ey, ex) = 0;
                }
        } else {
            for (int ex = x0; ex <= x1; ex++) {
                push_undo(ex, cursor_y);
                PX(cursor_y, ex) = 0;
            }
        }
        commit_undo_action();
        last_action = GDK_KEY_x;
        status_update();
        gtk_widget_queue_draw(GTK_WIDGET(data));
        return TRUE;
    }

    if (pending_f) {
        pending_f = FALSE;
        void (*fn)(void) = find_right;
        if (event->keyval == GDK_KEY_j) {
            last_find_dir = 2;  fn = find_down;
        } else if (event->keyval == GDK_KEY_k) {
            last_find_dir = -2; fn = find_up;
        } else if (event->keyval == GDK_KEY_h) {
            last_find_dir = -1; fn = find_left;
        } else {
            last_find_dir = 1;
        }
        for (int i = 0; i < f_count; i++) fn();
        status_update();
        gtk_widget_queue_draw(GTK_WIDGET(data));
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
        pending_g = FALSE;
        pending_d = FALSE;
        pending_f = FALSE;
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
        yank_buf = malloc(yank_w * yank_h);
        for (int ey = 0; ey < yank_h; ey++)
            for (int ex = 0; ex < yank_w; ex++)
                yank_buf[ey * yank_w + ex] = PX(y0 + ey, x0 + ex);
        visual_mode = FALSE;
        status_update();
        gtk_widget_queue_draw(GTK_WIDGET(data));
        return TRUE;
    }

    if (visual_mode && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_x)) {
        int x0 = MIN(cursor_x, visual_anchor_x);
        int x1 = MAX(cursor_x, visual_anchor_x);
        int y0 = MIN(cursor_y, visual_anchor_y);
        int y1 = MAX(cursor_y, visual_anchor_y);
        guchar val = (event->keyval == GDK_KEY_r) ? fg_color : 0;
        begin_undo_action();
        for (int ey = y0; ey <= y1; ey++)
            for (int ex = x0; ex <= x1; ex++) {
                push_undo(ex, ey);
                PX(ey, ex) = val;
            }
        commit_undo_action();
        last_action = event->keyval;
        visual_mode = FALSE;
        status_update();
        gtk_widget_queue_draw(GTK_WIDGET(data));
        return TRUE;
    }

    if (visual_mode && event->keyval == GDK_KEY_backslash) {
        int x0 = visual_anchor_x, y0 = visual_anchor_y;
        int x1 = cursor_x,        y1 = cursor_y;
        int dx = abs(x1 - x0), dy = abs(y1 - y0);
        int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx - dy;
        begin_undo_action();
        int cx = x0, cy = y0;
        while (1) {
            push_undo(cx, cy);
            PX(cy, cx) = fg_color;
            if (cx == x1 && cy == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; cx += sx; }
            if (e2 <  dx) { err += dx; cy += sy; }
        }
        commit_undo_action();
        last_action = GDK_KEY_r;
        visual_mode = FALSE;
        status_update();
        gtk_widget_queue_draw(GTK_WIDGET(data));
        return TRUE;
    }

    if (event->keyval == GDK_KEY_s && (event->state & GDK_CONTROL_MASK)) {
        if (*last_filename) cmd_write(last_filename);
        else cmd_flash("No filename. Use :w filename");
        return TRUE;
    }

    if (event->keyval == GDK_KEY_g && (event->state & GDK_CONTROL_MASK)) {
        char buf[512];
        snprintf(buf, sizeof(buf), "\"%s\"  %dx%d  col: %d  row: %d",
                 *last_filename ? last_filename : "[No Name]",
                 CANVAS_W, CANVAS_H, cursor_x + 1, cursor_y + 1);
        cmd_flash(buf);
        return TRUE;
    }

    if (event->keyval == GDK_KEY_r && (event->state & GDK_CONTROL_MASK)) {
        if (redo_count > 0) {
            redo_top--;
            redo_count--;
            int ridx = redo_top % UNDO_MAX;
            UndoAction a = redo_stack[ridx];
            redo_stack[ridx] = (UndoAction){NULL, 0};
            int uidx = undo_top % UNDO_MAX;
            if (undo_count == UNDO_MAX) free_action(&undo_stack[uidx]);
            undo_stack[uidx] = a;
            undo_top++;
            if (undo_count < UNDO_MAX) undo_count++;
            for (int i = 0; i < a.count; i++) {
                PX(a.changes[i].y, a.changes[i].x) = a.changes[i].after;
                cursor_x = a.changes[i].x;
                cursor_y = a.changes[i].y;
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
        gtk_main_quit();
        return TRUE;
    case GDK_KEY_plus:
    case GDK_KEY_equal:
        if (CELL_SIZE < 32) { CELL_SIZE += 2; zoom_resize(); }
        break;
    case GDK_KEY_minus:
        if (CELL_SIZE > 4) { CELL_SIZE -= 2; zoom_resize(); }
        break;
    case GDK_KEY_i:
        insert_mode = TRUE;
        break;
    case GDK_KEY_H:
        cursor_x = MAX(cursor_x - 5 * n, 0);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_h:
        cursor_x = MAX(cursor_x - n, 0);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_L:
        cursor_x = MIN(cursor_x + 5 * n, CANVAS_W - 1);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_l:
        cursor_x = MIN(cursor_x + n, CANVAS_W - 1);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_K:
        cursor_y = MAX(cursor_y - 5 * n, 0);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_k:
        cursor_y = MAX(cursor_y - n, 0);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_J:
        cursor_y = MIN(cursor_y + 5 * n, CANVAS_H - 1);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_j:
        cursor_y = MIN(cursor_y + n, CANVAS_H - 1);
        if (insert_mode) { begin_undo_action(); push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; commit_undo_action(); }
        break;
    case GDK_KEY_G:
        cursor_y = orig_count > 0 ? CLAMP(orig_count - 1, 0, CANVAS_H - 1) : CANVAS_H - 1;
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
        if      (last_find_dir ==  1) find_right();
        else if (last_find_dir == -1) find_left();
        else if (last_find_dir ==  2) find_down();
        else if (last_find_dir == -2) find_up();
        break;
    case GDK_KEY_N:
        if      (last_find_dir ==  1) find_left();
        else if (last_find_dir == -1) find_right();
        else if (last_find_dir ==  2) find_up();
        else if (last_find_dir == -2) find_down();
        break;
    case GDK_KEY_d:
        pending_d = TRUE;
        d_count = n;
        return TRUE;
    case GDK_KEY_c:
        fg_color = (fg_color % (PALETTE_SIZE - 1)) + 1;
        gtk_widget_queue_draw(palette_bar);
        flash_color(fg_color);
        break;
    case GDK_KEY_C:
        fg_color = (fg_color - 2 + (PALETTE_SIZE - 1)) % (PALETTE_SIZE - 1) + 1;
        gtk_widget_queue_draw(palette_bar);
        flash_color(fg_color);
        break;
    case GDK_KEY_e: {
        guchar idx = PX(cursor_y, cursor_x);
        if (idx > 0) {
            fg_color = idx;
            gtk_widget_queue_draw(palette_bar);
            flash_color(fg_color);
        }
        break;
    }
    case GDK_KEY_o: {
        int cx = cursor_x, cy = cursor_y, r = radius;
        begin_undo_action();
        if (r == 0) {
            push_undo(cx, cy); PX(cy, cx) = fg_color;
        } else {
            int bx = r, by = 0, err = 0;
#define CPSET(px, py) do { int _x=(px),_y=(py); \
    if (_x>=0&&_x<CANVAS_W&&_y>=0&&_y<CANVAS_H){push_undo(_x,_y);PX(_y,_x)=fg_color;} } while(0)
            while (bx >= by) {
                CPSET(cx+bx,cy+by); CPSET(cx+by,cy+bx);
                CPSET(cx-by,cy+bx); CPSET(cx-bx,cy+by);
                CPSET(cx-bx,cy-by); CPSET(cx-by,cy-bx);
                CPSET(cx+by,cy-bx); CPSET(cx+bx,cy-by);
                if (err <= 0) { by++; err += 2*by+1; }
                if (err >  0) { bx--; err -= 2*bx+1; }
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
                if ((ex-cx)*(ex-cx)+(ey-cy)*(ey-cy) <= r*r &&
                    ex >= 0 && ex < CANVAS_W && ey >= 0 && ey < CANVAS_H) {
                    push_undo(ex, ey); PX(ey, ex) = fg_color;
                }
        commit_undo_action();
        break;
    }
    case GDK_KEY_space:
    case GDK_KEY_r: {
        int x0 = CLAMP(cursor_x - radius, 0, CANVAS_W - 1);
        int x1 = CLAMP(cursor_x + radius, 0, CANVAS_W - 1);
        int y0 = CLAMP(cursor_y - radius, 0, CANVAS_H - 1);
        int y1 = CLAMP(cursor_y + radius, 0, CANVAS_H - 1);
        begin_undo_action();
        for (int ey = y0; ey <= y1; ey++)
            for (int ex = x0; ex <= x1; ex++) {
                push_undo(ex, ey);
                PX(ey, ex) = fg_color;
            }
        commit_undo_action();
        last_action = GDK_KEY_r;
        last_radius = radius;
        break;
    }
    case GDK_KEY_D:
        begin_undo_action();
        for (int ex = cursor_x; ex < CANVAS_W; ex++) {
            push_undo(ex, cursor_y);
            PX(cursor_y, ex) = 0;
        }
        commit_undo_action();
        last_action = GDK_KEY_x;
        break;
    case GDK_KEY_S:
        flood_fill(cursor_x, cursor_y, fg_color);
        break;
    case GDK_KEY_x: {
        int x0 = CLAMP(cursor_x - radius, 0, CANVAS_W - 1);
        int x1 = CLAMP(cursor_x + radius, 0, CANVAS_W - 1);
        int y0 = CLAMP(cursor_y - radius, 0, CANVAS_H - 1);
        int y1 = CLAMP(cursor_y + radius, 0, CANVAS_H - 1);
        begin_undo_action();
        for (int ey = y0; ey <= y1; ey++)
            for (int ex = x0; ex <= x1; ex++) {
                push_undo(ex, ey);
                PX(ey, ex) = 0;
            }
        commit_undo_action();
        last_action = GDK_KEY_x;
        last_radius = radius;
        break;
    }
    case GDK_KEY_period:
        if (last_action == GDK_KEY_r || last_action == GDK_KEY_x) {
            guchar val = (last_action == GDK_KEY_r) ? fg_color : 0;
            int x0 = CLAMP(cursor_x - last_radius, 0, CANVAS_W - 1);
            int x1 = CLAMP(cursor_x + last_radius, 0, CANVAS_W - 1);
            int y0 = CLAMP(cursor_y - last_radius, 0, CANVAS_H - 1);
            int y1 = CLAMP(cursor_y + last_radius, 0, CANVAS_H - 1);
            begin_undo_action();
            for (int ey = y0; ey <= y1; ey++)
                for (int ex = x0; ex <= x1; ex++) {
                    push_undo(ex, ey);
                    PX(ey, ex) = val;
                }
            commit_undo_action();
        }
        break;
    case GDK_KEY_p:
        if (yank_buf) {
            begin_undo_action();
            for (int ey = 0; ey < yank_h; ey++)
                for (int ex = 0; ex < yank_w; ex++) {
                    int px = cursor_x + ex, py = cursor_y + ey;
                    if (px < CANVAS_W && py < CANVAS_H) {
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
            int uidx = undo_top % UNDO_MAX;
            UndoAction a = undo_stack[uidx];
            undo_stack[uidx] = (UndoAction){NULL, 0};
            int ridx = redo_top % UNDO_MAX;
            if (redo_count == UNDO_MAX) free_action(&redo_stack[ridx]);
            redo_stack[ridx] = a;
            redo_top++;
            if (redo_count < UNDO_MAX) redo_count++;
            for (int i = a.count - 1; i >= 0; i--) {
                PX(a.changes[i].y, a.changes[i].x) = a.changes[i].before;
                cursor_x = a.changes[i].x;
                cursor_y = a.changes[i].y;
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

static gboolean on_palette_click(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    int idx = (int)(event->x / SWATCH_W);
    if (idx > 0 && idx < PALETTE_SIZE) {
        fg_color = (guchar)idx;
        gtk_widget_queue_draw(widget);
        flash_color(fg_color);
    }
    return TRUE;
}

static gboolean drag_painting = FALSE;

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    int cx = (int)(event->x / CELL_SIZE);
    int cy = (int)(event->y / CELL_SIZE);
    cursor_x = CLAMP(cx, 0, CANVAS_W - 1);
    cursor_y = CLAMP(cy, 0, CANVAS_H - 1);
    if (insert_mode) {
        begin_undo_action();
        drag_painting = TRUE;
        push_undo(cursor_x, cursor_y);
        PX(cursor_y, cursor_x) = fg_color;
    }
    status_update();
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    if (drag_painting) {
        commit_undo_action();
        drag_painting = FALSE;
    }
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (!(event->state & GDK_BUTTON1_MASK)) return TRUE;
    int cx = CLAMP((int)(event->x / CELL_SIZE), 0, CANVAS_W - 1);
    int cy = CLAMP((int)(event->y / CELL_SIZE), 0, CANVAS_H - 1);
    if (cx == cursor_x && cy == cursor_y) return TRUE;
    cursor_x = cx;
    cursor_y = cy;
    if (insert_mode && drag_painting) {
        push_undo(cursor_x, cursor_y);
        PX(cursor_y, cursor_x) = fg_color;
    }
    status_update();
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    static double accum = 0;
    double dy = 0;
    if (event->direction == GDK_SCROLL_SMOOTH) {
        double dx;
        gdk_event_get_scroll_deltas((GdkEvent *)event, &dx, &dy);
        accum += dy;
        if (accum > -3.0 && accum < 3.0) return TRUE;
        dy = accum;
        accum = 0;
    }
    if (dy < 0 && CELL_SIZE < 32) { CELL_SIZE += 2; zoom_resize(); }
    else if (dy > 0 && CELL_SIZE > 4) { CELL_SIZE -= 2; zoom_resize(); }
    return TRUE;
}

int main(int argc, char *argv[]) {
    /* Parse optional: vimpaint [width [height [cell_size]]] [file.png]
       Any argument that doesn't parse as a positive integer is the filename. */
    char *startup_file = NULL;
    if (argc >= 2) {
        int v = atoi(argv[1]);
        if (v > 0) CANVAS_W = v; else startup_file = argv[1];
    }
    if (argc >= 3 && !startup_file) {
        int v = atoi(argv[2]);
        if (v > 0) CANVAS_H = v; else startup_file = argv[2];
    }
    if (argc >= 4 && !startup_file) {
        int v = atoi(argv[3]);
        if (v > 0) CELL_SIZE = CLAMP(v, 4, 64); else startup_file = argv[3];
    }
    if (argc >= 5 && !startup_file)
        startup_file = argv[4];

    if (startup_file) {
        cairo_surface_t *probe = cairo_image_surface_create_from_png(startup_file);
        if (cairo_surface_status(probe) == CAIRO_STATUS_SUCCESS) {
            CANVAS_W = cairo_image_surface_get_width(probe);
            CANVAS_H = cairo_image_surface_get_height(probe);
        }
        cairo_surface_destroy(probe);
    }

    pixels = calloc(CANVAS_W * CANVAS_H, 1);

    gtk_init(&argc, &argv);

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *window = main_window;
    gtk_window_set_title(GTK_WINDOW(window), "vim-paint");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    main_canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(main_canvas, CANVAS_W * CELL_SIZE, CANVAS_H * CELL_SIZE);
    gtk_box_pack_start(GTK_BOX(vbox), main_canvas, FALSE, FALSE, 0);

    palette_bar = gtk_drawing_area_new();
    gtk_widget_set_size_request(palette_bar, CANVAS_W * CELL_SIZE, SWATCH_H);
    gtk_box_pack_start(GTK_BOX(vbox), palette_bar, FALSE, FALSE, 0);

    cmd_label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(cmd_label), 0.0);
    gtk_widget_set_size_request(cmd_label, CANVAS_W * CELL_SIZE, 20);
    gtk_box_pack_start(GTK_BOX(vbox), cmd_label, FALSE, FALSE, 0);

    gtk_widget_add_events(main_canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_BUTTON1_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_widget_add_events(palette_bar, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(main_canvas, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(main_canvas, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(main_canvas, "button-release-event", G_CALLBACK(on_button_release), NULL);
    g_signal_connect(main_canvas, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);
    g_signal_connect(main_canvas, "scroll-event", G_CALLBACK(on_scroll), NULL);
    g_signal_connect(palette_bar, "draw", G_CALLBACK(on_palette_draw), NULL);
    g_signal_connect(palette_bar, "button-press-event", G_CALLBACK(on_palette_click), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), main_canvas);

    gtk_widget_show_all(window);
    status_update();

    if (startup_file) {
        snprintf(cmd_buf, sizeof(cmd_buf), ":e %s", startup_file);
        cmd_len = strlen(cmd_buf);
        cmd_execute();
    }

    gtk_main();

    return 0;
}
