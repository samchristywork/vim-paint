#include "render.h"
#include "layers.h"
#include "main.h"
#include "palette.h"

gboolean on_palette_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
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

gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
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

  /* Onion skin: overlay adjacent layers semi-transparently as drawing
     reference. Previous layer is tinted red, next layer is tinted blue. */
  if (show_onionskin && layer_count > 1) {
    double op = onionskin_opacity / 100.0;
    int offsets[2] = {-1, 1};
    for (int oi = 0; oi < 2; oi++) {
      int li = layer_active + offsets[oi];
      if (li < 0 || li >= layer_count || !layer_bufs[li])
        continue;
      for (int y = 0; y < CANVAS_H; y++) {
        for (int x = 0; x < CANVAS_W; x++) {
          guint32 px = layer_bufs[li][y * CANVAS_W + x];
          double pa = (px & 0xff) / 255.0;
          if (pa < 0.01)
            continue;
          double pr = (px >> 24 & 0xff) / 255.0;
          double pg = (px >> 16 & 0xff) / 255.0;
          double pb = (px >> 8 & 0xff) / 255.0;
          double alpha = pa * op;
          /* Tint: previous=red, next=blue */
          if (offsets[oi] < 0)
            cairo_set_source_rgba(cr, pr * 0.5 + 0.5, pg * 0.5, pb * 0.5,
                                  alpha);
          else
            cairo_set_source_rgba(cr, pr * 0.5, pg * 0.5, pb * 0.5 + 0.5,
                                  alpha);
          cairo_rectangle(cr, x * CELL_SIZE, y * CELL_SIZE, CELL_SIZE,
                          CELL_SIZE);
          cairo_fill(cr);
        }
      }
    }
  }

  /* Draw grid lines */
  if (show_grid) {
    cairo_set_source_rgba(cr, ((grid_color >> 24) & 0xff) / 255.0,
                          ((grid_color >> 16) & 0xff) / 255.0,
                          ((grid_color >> 8) & 0xff) / 255.0,
                          (grid_color & 0xff) / 255.0);
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
    double g1 = (grad_c1 >> 8 & 0xff) / 255.0;
    double b1 = (grad_c1 & 0xff) / 255.0;
    double r2 = (grad_c2 >> 16 & 0xff) / 255.0;
    double g2 = (grad_c2 >> 8 & 0xff) / 255.0;
    double b2 = (grad_c2 & 0xff) / 255.0;
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

gboolean on_palette_click(GtkWidget *widget, GdkEventButton *event,
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
