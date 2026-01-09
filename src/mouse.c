#include "mouse.h"
#include "draw.h"
#include "main.h"
#include "undo.h"

static gboolean drag_painting = FALSE;
static gboolean drag_selecting = FALSE;
static guint32 drag_color = 0;

gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
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

gboolean on_button_release(GtkWidget *widget, GdkEventButton *event,
                           gpointer data) {
  if (grad_dragging) {
    grad_dragging = FALSE;
    apply_gradient_linear(grad_x0, grad_y0, cursor_x, cursor_y, grad_c1,
                          grad_c2);
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

gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
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

gboolean on_scroll(GtkWidget *widget, GdkEventScroll *event,
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
