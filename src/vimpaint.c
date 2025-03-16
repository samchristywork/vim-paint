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

static void status_update(void) {
    if (cmd_mode) return;
    char buf[64];
    const char *mode = visual_mode ? "VISUAL" : insert_mode ? "INSERT" : "NORMAL";
    snprintf(buf, sizeof(buf), " %s  col: %d  row: %d", mode, cursor_x + 1, cursor_y + 1);
    gtk_label_set_text(GTK_LABEL(cmd_label), buf);
}

static guint flash_timer_id = 0;

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

    if (strcmp(cmd_buf, ":wq") == 0) {
        if (*last_filename) cmd_write(last_filename);
        gtk_main_quit();
        return;
    }

    if (strncmp(cmd_buf, ":w ", 3) == 0 && *arg) {
        if (cmd_write(arg))
            snprintf(last_filename, sizeof(last_filename), "%s", arg);
        return;
    }

    if (strncmp(cmd_buf, ":wq ", 4) == 0 && *arg) {
        if (cmd_write(arg))
            snprintf(last_filename, sizeof(last_filename), "%s", arg);
        gtk_main_quit();
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
        snprintf(last_filename, sizeof(last_filename), "%s", arg);
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
            if (val[0] == '#' && strlen(val) == 7) {
                unsigned int rgb;
                if (sscanf(val + 1, "%06x", &rgb) == 1) {
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
                            cmd_set("");
                        }
                    } else {
                        fg_color = (guchar)found;
                        gtk_widget_queue_draw(palette_bar);
                        cmd_set("");
                    }
                } else {
                    cmd_flash("Invalid hex color.");
                }
            } else {
                int n = atoi(val);
                if (n >= 0 && n < PALETTE_SIZE) {
                    fg_color = (guchar)n;
                    gtk_widget_queue_draw(palette_bar);
                    cmd_set("");
                } else {
                    cmd_flash("Color index out of range.");
                }
            }
        } else {
            cmd_flash("Unknown option.");
        }
        return;
    }

    cmd_flash("Unknown command.");
}

static guchar *yank_buf = NULL;
static int yank_w = 0;
static int yank_h = 0;

#define UNDO_MAX 256
typedef struct { int x, y; guchar old_val; } UndoEntry;
static UndoEntry undo_stack[UNDO_MAX];
static int undo_top = 0;
static int undo_count = 0;
static UndoEntry redo_stack[UNDO_MAX];
static int redo_top = 0;
static int redo_count = 0;

static void push_undo(int x, int y) {
    undo_stack[undo_top % UNDO_MAX] = (UndoEntry){x, y, PX(y, x)};
    undo_top++;
    if (undo_count < UNDO_MAX) undo_count++;
    redo_top = 0;
    redo_count = 0;
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

    if (event->keyval == GDK_KEY_colon) {
        cmd_mode = TRUE;
        cmd_buf[0] = ':'; cmd_buf[1] = '\0'; cmd_len = 1;
        cmd_set(cmd_buf);
        return TRUE;
    }

    static gboolean pending_g = FALSE;
    static gboolean pending_d = FALSE;
    static int d_count = 1;
    static int count = 0;
    static guint last_action = 0;
    static int last_find_dir = 0;  /* +1 = forward (f), -1 = backward (F), 0 = none */

    if (pending_g) {
        pending_g = FALSE;
        if (event->keyval == GDK_KEY_g) {
            cursor_y = 0;
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
        last_action = GDK_KEY_x;
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

    int n = count > 0 ? count : 1;
    count = 0;

    if (event->keyval == GDK_KEY_v && (event->state & GDK_CONTROL_MASK)) {
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
        for (int ey = y0; ey <= y1; ey++)
            for (int ex = x0; ex <= x1; ex++) {
                push_undo(ex, ey);
                PX(ey, ex) = val;
            }
        last_action = event->keyval;
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

    if (event->keyval == GDK_KEY_r && (event->state & GDK_CONTROL_MASK)) {
        if (redo_count > 0) {
            redo_top--;
            redo_count--;
            UndoEntry e = redo_stack[redo_top % UNDO_MAX];
            undo_stack[undo_top % UNDO_MAX] = (UndoEntry){e.x, e.y, PX(e.y, e.x)};
            undo_top++;
            if (undo_count < UNDO_MAX) undo_count++;
            PX(e.y, e.x) = e.old_val;
            cursor_x = e.x;
            cursor_y = e.y;
            status_update();
            gtk_widget_queue_draw(GTK_WIDGET(data));
        }
        return TRUE;
    }

    if (event->keyval == GDK_KEY_g) {
        pending_g = TRUE;
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
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_h:
        cursor_x = MAX(cursor_x - n, 0);
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_L:
        cursor_x = MIN(cursor_x + 5 * n, CANVAS_W - 1);
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_l:
        cursor_x = MIN(cursor_x + n, CANVAS_W - 1);
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_K:
        cursor_y = MAX(cursor_y - 5 * n, 0);
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_k:
        cursor_y = MAX(cursor_y - n, 0);
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_J:
        cursor_y = MIN(cursor_y + 5 * n, CANVAS_H - 1);
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_j:
        cursor_y = MIN(cursor_y + n, CANVAS_H - 1);
        if (insert_mode) { push_undo(cursor_x, cursor_y); PX(cursor_y, cursor_x) = fg_color; }
        break;
    case GDK_KEY_G:
        cursor_y = CANVAS_H - 1;
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
        last_find_dir = 1;
        for (int fx = cursor_x + 1; fx < CANVAS_W; fx++) {
            if (PX(cursor_y, fx)) { cursor_x = fx; break; }
        }
        break;
    case GDK_KEY_F:
        last_find_dir = -1;
        for (int fx = cursor_x - 1; fx >= 0; fx--) {
            if (PX(cursor_y, fx)) { cursor_x = fx; break; }
        }
        break;
    case GDK_KEY_n:
        if (last_find_dir > 0) {
            for (int fx = cursor_x + 1; fx < CANVAS_W; fx++)
                if (PX(cursor_y, fx)) { cursor_x = fx; break; }
        } else if (last_find_dir < 0) {
            for (int fx = cursor_x - 1; fx >= 0; fx--)
                if (PX(cursor_y, fx)) { cursor_x = fx; break; }
        }
        break;
    case GDK_KEY_N:
        if (last_find_dir > 0) {
            for (int fx = cursor_x - 1; fx >= 0; fx--)
                if (PX(cursor_y, fx)) { cursor_x = fx; break; }
        } else if (last_find_dir < 0) {
            for (int fx = cursor_x + 1; fx < CANVAS_W; fx++)
                if (PX(cursor_y, fx)) { cursor_x = fx; break; }
        }
        break;
    case GDK_KEY_d:
        pending_d = TRUE;
        d_count = n;
        return TRUE;
    case GDK_KEY_c:
        fg_color = (fg_color % (PALETTE_SIZE - 1)) + 1;
        gtk_widget_queue_draw(palette_bar);
        break;
    case GDK_KEY_C:
        fg_color = (fg_color - 2 + (PALETTE_SIZE - 1)) % (PALETTE_SIZE - 1) + 1;
        gtk_widget_queue_draw(palette_bar);
        break;
    case GDK_KEY_r:
        push_undo(cursor_x, cursor_y);
        PX(cursor_y, cursor_x) = fg_color;
        last_action = GDK_KEY_r;
        break;
    case GDK_KEY_x:
        push_undo(cursor_x, cursor_y);
        PX(cursor_y, cursor_x) = 0;
        last_action = GDK_KEY_x;
        break;
    case GDK_KEY_period:
        if (last_action == GDK_KEY_r || last_action == GDK_KEY_x) {
            push_undo(cursor_x, cursor_y);
            PX(cursor_y, cursor_x) = (last_action == GDK_KEY_r) ? fg_color : 0;
        }
        break;
    case GDK_KEY_p:
        if (yank_buf) {
            for (int ey = 0; ey < yank_h; ey++)
                for (int ex = 0; ex < yank_w; ex++) {
                    int px = cursor_x + ex, py = cursor_y + ey;
                    if (px < CANVAS_W && py < CANVAS_H) {
                        push_undo(px, py);
                        PX(py, px) = yank_buf[ey * yank_w + ex];
                    }
                }
        }
        break;
    case GDK_KEY_u:
        if (undo_count > 0) {
            undo_top--;
            undo_count--;
            UndoEntry e = undo_stack[undo_top % UNDO_MAX];
            redo_stack[redo_top % UNDO_MAX] = (UndoEntry){e.x, e.y, PX(e.y, e.x)};
            redo_top++;
            if (redo_count < UNDO_MAX) redo_count++;
            PX(e.y, e.x) = e.old_val;
            cursor_x = e.x;
            cursor_y = e.y;
        }
        break;
    default:
        return FALSE;
    }

    status_update();
    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    int cx = (int)(event->x / CELL_SIZE);
    int cy = (int)(event->y / CELL_SIZE);
    cursor_x = CLAMP(cx, 0, CANVAS_W - 1);
    cursor_y = CLAMP(cy, 0, CANVAS_H - 1);
    if (insert_mode) {
        push_undo(cursor_x, cursor_y);
        PX(cursor_y, cursor_x) = fg_color;
    }
    status_update();
    gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    if (!(event->state & GDK_BUTTON1_MASK)) return TRUE;
    int cx = CLAMP((int)(event->x / CELL_SIZE), 0, CANVAS_W - 1);
    int cy = CLAMP((int)(event->y / CELL_SIZE), 0, CANVAS_H - 1);
    if (cx == cursor_x && cy == cursor_y) return TRUE;
    cursor_x = cx;
    cursor_y = cy;
    if (insert_mode) {
        push_undo(cursor_x, cursor_y);
        PX(cursor_y, cursor_x) = fg_color;
    }
    status_update();
    gtk_widget_queue_draw(widget);
    return TRUE;
}

int main(int argc, char *argv[]) {
    /* Parse optional: vimpaint [width [height]] */
    if (argc >= 2) CANVAS_W = MAX(1, atoi(argv[1]));
    if (argc >= 3) CANVAS_H = MAX(1, atoi(argv[2]));

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

    gtk_widget_add_events(main_canvas, GDK_BUTTON_PRESS_MASK | GDK_BUTTON1_MOTION_MASK);
    g_signal_connect(main_canvas, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(main_canvas, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(main_canvas, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);
    g_signal_connect(palette_bar, "draw", G_CALLBACK(on_palette_draw), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), main_canvas);

    gtk_widget_show_all(window);
    status_update();
    gtk_main();

    return 0;
}
