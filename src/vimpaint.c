#include <gtk/gtk.h>
#include <string.h>

#define CANVAS_W 80
#define CANVAS_H 40
#define CELL_SIZE 12

static guchar pixels[CANVAS_H][CANVAS_W];  /* 0 = white, 1 = black */
static int cursor_x = 0;
static int cursor_y = 0;
static gboolean visual_mode = FALSE;
static int visual_anchor_x = 0;
static int visual_anchor_y = 0;
static gboolean insert_mode = FALSE;

#define UNDO_MAX 256
typedef struct { int x, y; guchar old_val; } UndoEntry;
static UndoEntry undo_stack[UNDO_MAX];
static int undo_top = 0;
static UndoEntry redo_stack[UNDO_MAX];
static int redo_top = 0;

static void push_undo(int x, int y) {
    undo_stack[undo_top % UNDO_MAX] = (UndoEntry){x, y, pixels[y][x]};
    undo_top++;
    redo_top = 0;
}

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
            if (pixels[y][x]) {
                cairo_set_source_rgb(cr, 0, 0, 0);
            } else {
                cairo_set_source_rgb(cr, 1, 1, 1);
            }
            cairo_rectangle(cr, x * CELL_SIZE, y * CELL_SIZE, CELL_SIZE, CELL_SIZE);
            cairo_fill(cr);
        }
    }

    /* Draw grid lines */
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
        if (pixels[cursor_y][cursor_x]) {
            cairo_set_source_rgb(cr, 1, 1, 1);
        } else {
            cairo_set_source_rgb(cr, 1, 0, 0);
        }
        cairo_set_line_width(cr, 2.0);
        cairo_rectangle(cr, cursor_x * CELL_SIZE + 1, cursor_y * CELL_SIZE + 1,
                        CELL_SIZE - 2, CELL_SIZE - 2);
        cairo_stroke(cr);
    }

    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    static gboolean pending_g = FALSE;
    static gboolean pending_d = FALSE;
    static int d_count = 1;
    static int count = 0;
    static guint last_action = 0;

    if (pending_g) {
        pending_g = FALSE;
        if (event->keyval == GDK_KEY_g) {
            cursor_y = 0;
            count = 0;
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
                    pixels[ey][ex] = 0;
                }
        } else {
            for (int ex = x0; ex <= x1; ex++) {
                push_undo(ex, cursor_y);
                pixels[cursor_y][ex] = 0;
            }
        }
        last_action = GDK_KEY_x;
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
        gtk_widget_queue_draw(GTK_WIDGET(data));
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Escape) {
        visual_mode = FALSE;
        insert_mode = FALSE;
        gtk_widget_queue_draw(GTK_WIDGET(data));
        return TRUE;
    }

    if (visual_mode && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_x)) {
        int x0 = MIN(cursor_x, visual_anchor_x);
        int x1 = MAX(cursor_x, visual_anchor_x);
        int y0 = MIN(cursor_y, visual_anchor_y);
        int y1 = MAX(cursor_y, visual_anchor_y);
        guchar val = (event->keyval == GDK_KEY_r) ? 1 : 0;
        for (int ey = y0; ey <= y1; ey++)
            for (int ex = x0; ex <= x1; ex++) {
                push_undo(ex, ey);
                pixels[ey][ex] = val;
            }
        last_action = event->keyval;
        visual_mode = FALSE;
        gtk_widget_queue_draw(GTK_WIDGET(data));
        return TRUE;
    }

    if (event->keyval == GDK_KEY_r && (event->state & GDK_CONTROL_MASK)) {
        if (redo_top > 0) {
            redo_top--;
            UndoEntry e = redo_stack[redo_top % UNDO_MAX];
            undo_stack[undo_top % UNDO_MAX] = (UndoEntry){e.x, e.y, pixels[e.y][e.x]};
            undo_top++;
            pixels[e.y][e.x] = e.old_val;
            cursor_x = e.x;
            cursor_y = e.y;
            gtk_widget_queue_draw(GTK_WIDGET(data));
        }
        return TRUE;
    }

    if (event->keyval == GDK_KEY_g) {
        pending_g = TRUE;
        return TRUE;
    }

    switch (event->keyval) {
    case GDK_KEY_i:
        insert_mode = TRUE;
        break;
    case GDK_KEY_h:
        cursor_x = MAX(cursor_x - n, 0);
        if (insert_mode) { push_undo(cursor_x, cursor_y); pixels[cursor_y][cursor_x] = 1; }
        break;
    case GDK_KEY_l:
        cursor_x = MIN(cursor_x + n, CANVAS_W - 1);
        if (insert_mode) { push_undo(cursor_x, cursor_y); pixels[cursor_y][cursor_x] = 1; }
        break;
    case GDK_KEY_k:
        cursor_y = MAX(cursor_y - n, 0);
        if (insert_mode) { push_undo(cursor_x, cursor_y); pixels[cursor_y][cursor_x] = 1; }
        break;
    case GDK_KEY_j:
        cursor_y = MIN(cursor_y + n, CANVAS_H - 1);
        if (insert_mode) { push_undo(cursor_x, cursor_y); pixels[cursor_y][cursor_x] = 1; }
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
        for (int fx = cursor_x + 1; fx < CANVAS_W; fx++) {
            if (pixels[cursor_y][fx]) {
                cursor_x = fx;
                break;
            }
        }
        break;
    case GDK_KEY_d:
        pending_d = TRUE;
        d_count = n;
        return TRUE;
    case GDK_KEY_r:
        push_undo(cursor_x, cursor_y);
        pixels[cursor_y][cursor_x] = 1;
        last_action = GDK_KEY_r;
        break;
    case GDK_KEY_x:
        push_undo(cursor_x, cursor_y);
        pixels[cursor_y][cursor_x] = 0;
        last_action = GDK_KEY_x;
        break;
    case GDK_KEY_period:
        if (last_action == GDK_KEY_r || last_action == GDK_KEY_x) {
            push_undo(cursor_x, cursor_y);
            pixels[cursor_y][cursor_x] = (last_action == GDK_KEY_r) ? 1 : 0;
        }
        break;
    case GDK_KEY_u:
        if (undo_top > 0) {
            undo_top--;
            UndoEntry e = undo_stack[undo_top % UNDO_MAX];
            redo_stack[redo_top % UNDO_MAX] = (UndoEntry){e.x, e.y, pixels[e.y][e.x]};
            redo_top++;
            pixels[e.y][e.x] = e.old_val;
            cursor_x = e.x;
            cursor_y = e.y;
        }
        break;
    default:
        return FALSE;
    }

    gtk_widget_queue_draw(GTK_WIDGET(data));
    return TRUE;
}

int main(int argc, char *argv[]) {
    memset(pixels, 0, sizeof(pixels));

    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "vim-paint");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(canvas, CANVAS_W * CELL_SIZE, CANVAS_H * CELL_SIZE);
    gtk_container_add(GTK_CONTAINER(window), canvas);

    g_signal_connect(canvas, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), canvas);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
