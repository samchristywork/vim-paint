#include <gtk/gtk.h>
#include <string.h>

#define CANVAS_W 80
#define CANVAS_H 40
#define CELL_SIZE 12

static guchar pixels[CANVAS_H][CANVAS_W];  /* 0 = white, 1 = black */
static int cursor_x = 0;
static int cursor_y = 0;

#define UNDO_MAX 256
typedef struct { int x, y; guchar old_val; } UndoEntry;
static UndoEntry undo_stack[UNDO_MAX];
static int undo_top = 0;

static void push_undo(int x, int y) {
    undo_stack[undo_top % UNDO_MAX] = (UndoEntry){x, y, pixels[y][x]};
    undo_top++;
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

    /* Draw cursor as an outline that contrasts with the underlying pixel */
    if (pixels[cursor_y][cursor_x]) {
        cairo_set_source_rgb(cr, 1, 1, 1);
    } else {
        cairo_set_source_rgb(cr, 1, 0, 0);
    }
    cairo_set_line_width(cr, 2.0);
    cairo_rectangle(cr, cursor_x * CELL_SIZE + 1, cursor_y * CELL_SIZE + 1,
                    CELL_SIZE - 2, CELL_SIZE - 2);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    static gboolean pending_g = FALSE;
    static int count = 0;

    if (pending_g) {
        pending_g = FALSE;
        if (event->keyval == GDK_KEY_g) {
            cursor_y = 0;
            count = 0;
            gtk_widget_queue_draw(GTK_WIDGET(data));
            return TRUE;
        }
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

    if (event->keyval == GDK_KEY_g) {
        pending_g = TRUE;
        return TRUE;
    }

    switch (event->keyval) {
    case GDK_KEY_h:
        cursor_x = MAX(cursor_x - n, 0);
        break;
    case GDK_KEY_l:
        cursor_x = MIN(cursor_x + n, CANVAS_W - 1);
        break;
    case GDK_KEY_k:
        cursor_y = MAX(cursor_y - n, 0);
        break;
    case GDK_KEY_j:
        cursor_y = MIN(cursor_y + n, CANVAS_H - 1);
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
    case GDK_KEY_r:
        push_undo(cursor_x, cursor_y);
        pixels[cursor_y][cursor_x] = 1;
        break;
    case GDK_KEY_x:
        push_undo(cursor_x, cursor_y);
        pixels[cursor_y][cursor_x] = 0;
        break;
    case GDK_KEY_u:
        if (undo_top > 0) {
            undo_top--;
            UndoEntry e = undo_stack[undo_top % UNDO_MAX];
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
