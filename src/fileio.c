#include "vimpaint.h"

gboolean cmd_write(const char *filename) {
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

void cmd_export_bmp(const char *filename, int scale) {
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

void cmd_open(const char *filename) {
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

void exec_write(const char *arg) {
  if (*arg) {
    if (cmd_write(arg)) {
      snprintf(last_filename, sizeof(last_filename), "%s", arg);
      update_title(last_filename);
    }
  } else {
    if (*last_filename)
      cmd_write(last_filename);
    else
      cmd_flash("No filename. Use :w filename");
  }
}

void exec_write_quit(const char *arg) {
  if (*arg) {
    if (cmd_write(arg)) {
      snprintf(last_filename, sizeof(last_filename), "%s", arg);
      update_title(last_filename);
      gtk_main_quit();
    }
  } else {
    if (*last_filename) {
      if (cmd_write(last_filename))
        gtk_main_quit();
    } else {
      cmd_flash("No filename. Use :wq filename");
    }
  }
}

void exec_edit(const char *arg) {
  gboolean force = (cmd_buf[2] == '!');
  if (!force && canvas_dirty) {
    cmd_flash("Unsaved changes. Use :e! to discard or :w to save first.");
    return;
  }
  const char *fn = *arg ? arg : last_filename;
  if (*fn)
    cmd_open(fn);
  else
    cmd_flash("No filename.");
}

void exec_export(const char *arg) {
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
}
