#include "vimpaint.h"

static void get_vis_rect(int *x0, int *y0, int *x1, int *y1) {
  *x0 = 0;
  *y0 = 0;
  *x1 = CANVAS_W - 1;
  *y1 = CANVAS_H - 1;
  if (visual_mode) {
    *x0 = MIN(cursor_x, visual_anchor_x);
    *x1 = MAX(cursor_x, visual_anchor_x);
    *y0 = MIN(cursor_y, visual_anchor_y);
    *y1 = MAX(cursor_y, visual_anchor_y);
  }
}

static void exec_quit(const char *arg) {
  (void)arg;
  if (canvas_dirty)
    cmd_flash(
        "Unsaved changes. Use :q! to force quit or :wq to save and quit.");
  else if (tab_count > 1)
    tab_close_current();
  else
    gtk_main_quit();
}

static void exec_force_quit(const char *arg) {
  (void)arg;
  if (tab_count > 1)
    tab_close_current();
  else
    gtk_main_quit();
}

static void exec_tabnew(const char *arg) {
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
  tab_save(tab_current);
  if (*arg)
    cmd_open(arg);
  zoom_resize();
  title_refresh();
  status_update();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

static void exec_new(const char *arg) {
  (void)arg;
  gboolean force = (cmd_buf[4] == '!');
  if (!force && canvas_dirty) {
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
}

static void exec_write(const char *arg) {
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

static void exec_write_quit(const char *arg) {
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

static void exec_edit(const char *arg) {
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

static void exec_set(const char *arg) {
  const char *opt = cmd_buf + 5;
  (void)arg;
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
      grid_color =
          PACK_RGBA((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
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
  } else if (strcmp(opt, "onionskin") == 0) {
    show_onionskin = TRUE;
    gtk_widget_queue_draw(main_canvas);
    cmd_flash("Onion skin on.");
  } else if (strcmp(opt, "noonionskin") == 0) {
    show_onionskin = FALSE;
    gtk_widget_queue_draw(main_canvas);
    cmd_flash("Onion skin off.");
  } else if (strncmp(opt, "onionskinopacity ", 17) == 0) {
    int v = atoi(opt + 17);
    if (v < 1 || v > 100) {
      cmd_flash("Onion skin opacity must be 1-100.");
    } else {
      onionskin_opacity = v;
      gtk_widget_queue_draw(main_canvas);
      cmd_set("");
    }
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
  } else if (strncmp(opt, "ellipsery ", 10) == 0) {
    const char *val = opt + 10;
    if (strcmp(val, "0") == 0 || strcmp(val, "off") == 0) {
      ellipse_ry = 0;
      cmd_flash("Ellipse ry: auto (= rx)");
    } else {
      int v = atoi(val);
      if (v >= 1 && v <= 8192) {
        ellipse_ry = v;
        cmd_flash("Ellipse ry set");
      } else {
        cmd_flash("Usage: :set ellipsery <1-8192>|0");
      }
    }
  } else if (strncmp(opt, "fill ", 5) == 0) {
    const char *val = opt + 5;
    if (strcmp(val, "solid") == 0) {
      fill_pattern = FILL_SOLID;
      cmd_flash("Fill: solid");
    } else if (strcmp(val, "checker") == 0) {
      fill_pattern = FILL_CHECKER;
      cmd_flash("Fill: checkerboard");
    } else if (strcmp(val, "hstripes") == 0) {
      fill_pattern = FILL_HSTRIPES;
      cmd_flash("Fill: horizontal stripes");
    } else if (strcmp(val, "vstripes") == 0) {
      fill_pattern = FILL_VSTRIPES;
      cmd_flash("Fill: vertical stripes");
    } else if (strcmp(val, "halftone") == 0) {
      fill_pattern = FILL_HALFTONE;
      cmd_flash("Fill: halftone dither");
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
    if (n < 2 || n > 32)
      n = 4;
    sym_radial_n = n;
    sym_mode = SYM_RADIAL;
    char msg[48];
    snprintf(msg, sizeof(msg), "Symmetry: %d-point radial", sym_radial_n);
    cmd_flash(msg);
  } else if (strcmp(opt, "sym none") == 0 || strcmp(opt, "nosym") == 0) {
    sym_mode = SYM_NONE;
    cmd_flash("Symmetry: off");
  } else if (strncmp(opt, "font ", 5) == 0) {
    const char *farg = opt + 5;
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
}

static void exec_replace(const char *arg) {
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
}

static void exec_gradient(const char *arg) {
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
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
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
}

static void exec_gradtool(const char *arg) {
  unsigned int rgb1 = 0, rgb2 = 0;
  if (*arg) {
    char c1s[64] = "", c2s[64] = "";
    if (sscanf(arg, "%63s %63s", c1s, c2s) != 2 || !parse_color(c1s, &rgb1) ||
        !parse_color(c2s, &rgb2)) {
      cmd_flash("Usage: :gradtool [color1 color2]");
      return;
    }
  } else {
    rgb1 = ((fg_color >> 8) & 0xffffff);
    rgb2 = ((bg_color >> 8) & 0xffffff);
  }
  grad_c1 = rgb1;
  grad_c2 = rgb2;
  gradient_tool = TRUE;
  grad_dragging = FALSE;
  cmd_flash("Click and drag to apply gradient (Esc to cancel)");
}

static void exec_brushdefine(const char *arg) {
  if (*arg) {
    memset(custom_brush_pixels, 0, sizeof(custom_brush_pixels));
    int row = 0, maxcol = 0;
    const char *p = arg;
#define CUSTOM_BRUSH_MAX 16
    while (*p && row < CUSTOM_BRUSH_MAX) {
      int col = 0;
      while (*p && *p != '/' && col < CUSTOM_BRUSH_MAX) {
        custom_brush_pixels[row][col] =
            (*p == '#' || *p == '*' || *p == '1') ? TRUE : FALSE;
        col++;
        p++;
      }
      if (col > maxcol)
        maxcol = col;
      row++;
      if (*p == '/')
        p++;
    }
#undef CUSTOM_BRUSH_MAX
    if (row == 0 || maxcol == 0) {
      cmd_flash("Empty pattern.");
      return;
    }
    custom_brush_w = maxcol;
    custom_brush_h = row;
  } else if (visual_mode) {
    int x0 = MIN(cursor_x, visual_anchor_x);
    int x1 = MAX(cursor_x, visual_anchor_x);
    int y0 = MIN(cursor_y, visual_anchor_y);
    int y1 = MAX(cursor_y, visual_anchor_y);
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    if (w > 16)
      w = 16;
    if (h > 16)
      h = 16;
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
  snprintf(msg, sizeof(msg), "Custom brush defined (%dx%d).", custom_brush_w,
           custom_brush_h);
  cmd_flash(msg);
}

static void exec_hsl(const char *arg) {
  double delta = atof(arg);
  int is_hue = (cmd_buf[1] == 'h');
  int is_sat = (cmd_buf[1] == 's');
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
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
}

static void exec_dither(const char *arg) {
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
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
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
}

static void exec_find_color(const char *arg) {
  (void)arg;
  const char *val = cmd_buf + 12;
  unsigned int rgb;
  if (!parse_color(val, &rgb)) {
    cmd_flash("Unknown color.");
    return;
  }
  int tr = (rgb >> 16) & 0xff;
  int tg = (rgb >> 8) & 0xff;
  int tb = rgb & 0xff;
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
}

static void exec_goto(const char *arg) {
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
}

static void exec_scale(const char *arg) {
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
}

static void exec_newlayer(const char *arg) {
  (void)arg;
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
  int ins = layer_active + 1;
  for (int li = layer_count; li > ins; li--) {
    layer_bufs[li] = layer_bufs[li - 1];
    layer_visible[li] = layer_visible[li - 1];
    layer_blend[li] = layer_blend[li - 1];
    layer_opacity[li] = layer_opacity[li - 1];
    memcpy(layer_name[li], layer_name[li - 1], 32);
  }
  layer_bufs[ins] = buf;
  layer_visible[ins] = TRUE;
  layer_blend[ins] = BLEND_NORMAL;
  layer_opacity[ins] = 100;
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
}

static void exec_seltolay(const char *arg) {
  (void)arg;
  if (!visual_mode) {
    cmd_flash("No selection. Enter visual mode first.");
    return;
  }
  if (layer_count >= LAYER_MAX) {
    cmd_flash("Layer limit reached.");
    return;
  }
  int x0 = MIN(cursor_x, visual_anchor_x);
  int x1 = MAX(cursor_x, visual_anchor_x);
  int y0 = MIN(cursor_y, visual_anchor_y);
  int y1 = MAX(cursor_y, visual_anchor_y);
  size_t total = (size_t)CANVAS_W * CANVAS_H;
  guint32 *buf = calloc(total, sizeof(guint32));
  if (!buf) {
    cmd_flash("Out of memory.");
    return;
  }
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++)
      buf[y * CANVAS_W + x] = PX(y, x);
  int ins = layer_active + 1;
  for (int li = layer_count; li > ins; li--) {
    layer_bufs[li] = layer_bufs[li - 1];
    layer_visible[li] = layer_visible[li - 1];
    layer_blend[li] = layer_blend[li - 1];
    layer_opacity[li] = layer_opacity[li - 1];
    memcpy(layer_name[li], layer_name[li - 1], 32);
  }
  layer_bufs[ins] = buf;
  layer_visible[ins] = TRUE;
  layer_blend[ins] = BLEND_NORMAL;
  layer_opacity[ins] = 100;
  snprintf(layer_name[ins], 32, "Layer %d", layer_count + 1);
  layer_count++;
  layer_active = ins;
  pixels = layer_bufs[layer_active];
  visual_mode = FALSE;
  clear_history();
  status_update();
  gtk_widget_queue_draw(main_canvas);
  char msg[64];
  snprintf(msg, sizeof(msg), "Selection copied to layer %d/%d",
           layer_active + 1, layer_count);
  cmd_flash(msg);
}

static void exec_mergedown(const char *arg) {
  (void)arg;
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
  for (int i = 0; i < (int)total; i++) {
    guint32 s = src[i];
    double sa = (s & 0xff) / 255.0;
    if (sa == 0.0)
      continue;
    guint32 d = dst[i];
    double da = (d & 0xff) / 255.0;
    double ra = sa + da * (1.0 - sa);
    if (ra < 1e-6) {
      dst[i] = 0;
      continue;
    }
    double inv = 1.0 - sa;
    int rr = (int)(((s >> 24 & 0xff) / 255.0 * sa +
                    (d >> 24 & 0xff) / 255.0 * da * inv) /
                       ra * 255 +
                   0.5);
    int rg = (int)(((s >> 16 & 0xff) / 255.0 * sa +
                    (d >> 16 & 0xff) / 255.0 * da * inv) /
                       ra * 255 +
                   0.5);
    int rb = (int)(((s >> 8 & 0xff) / 255.0 * sa +
                    (d >> 8 & 0xff) / 255.0 * da * inv) /
                       ra * 255 +
                   0.5);
    dst[i] =
        PACK_RGBA(CLAMP(rr, 0, 255), CLAMP(rg, 0, 255), CLAMP(rb, 0, 255),
                  CLAMP((int)(ra * 255 + 0.5), 0, 255));
  }
  free(layer_bufs[layer_active]);
  for (int li = layer_active; li < layer_count - 1; li++) {
    layer_bufs[li] = layer_bufs[li + 1];
    layer_visible[li] = layer_visible[li + 1];
    layer_blend[li] = layer_blend[li + 1];
    layer_opacity[li] = layer_opacity[li + 1];
    memcpy(layer_name[li], layer_name[li + 1], 32);
  }
  layer_count--;
  layer_bufs[layer_count] = NULL;
  layer_visible[layer_count] = FALSE;
  layer_blend[layer_count] = BLEND_NORMAL;
  layer_opacity[layer_count] = 100;
  layer_active--;
  pixels = layer_bufs[layer_active];
  commit_canvas_snapshot(before, CANVAS_W, CANVAS_H);
  status_update();
  gtk_widget_queue_draw(main_canvas);
  cmd_flash("Merged down.");
}

static void exec_layer(const char *arg) {
  int n = atoi(arg) - 1;
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
}

static void exec_layervis(const char *arg) {
  int n = atoi(arg) - 1;
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
}

static void exec_layerblend(const char *arg) {
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
      if (m)
        strncat(modes, "|", sizeof(modes) - strlen(modes) - 1);
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
}

static void exec_layeropacity(const char *arg) {
  int v = atoi(arg);
  if (v < 0 || v > 100) {
    cmd_flash("Opacity must be 0-100.");
    return;
  }
  layer_opacity[layer_active] = v;
  gtk_widget_queue_draw(main_canvas);
  status_update();
  char msg[64];
  snprintf(msg, sizeof(msg), "Layer %d opacity: %d%%", layer_active + 1, v);
  cmd_flash(msg);
}

static void exec_guide(const char *arg) {
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
  if (sscanf(arg, "%c %d", &dir, &coord) == 2 && (dir == 'h' || dir == 'v') &&
      coord >= 1) {
    gboolean horiz = (dir == 'h');
    int c = coord - 1;
    int limit = horiz ? CANVAS_H : CANVAS_W;
    if (c < 0 || c >= limit) {
      cmd_flash("Guide coordinate out of range.");
      return;
    }
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
}

static void exec_resize(const char *arg) {
  int nw = 0, nh = 0;
  if (sscanf(arg, "%dx%d", &nw, &nh) != 2)
    sscanf(arg, "%d %d", &nw, &nh);
  if (nw < 1 || nh < 1 || nw > 16384 || nh > 16384) {
    cmd_flash("Usage: :resize WxH  (max 16384)");
    return;
  }
  if (layer_count > 1)
    layers_flatten();
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
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
}

static void exec_savep(const char *arg) {
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
}

static void exec_loadp(const char *arg) {
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
}

static void exec_importp(const char *arg) {
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
}

static void exec_delp(const char *arg) {
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
}

static void exec_colorpicker(const char *arg) {
  (void)arg;
  gboolean set_bg = (strstr(cmd_buf, "bg") != NULL);
  guint32 current = set_bg ? bg_color : fg_color;
  GdkRGBA rgba = {((current >> 24) & 0xff) / 255.0,
                  ((current >> 16) & 0xff) / 255.0,
                  ((current >> 8) & 0xff) / 255.0, 1.0};
  GtkWidget *dlg = gtk_color_chooser_dialog_new(set_bg ? "Background Color"
                                                       : "Foreground Color",
                                                GTK_WINDOW(main_window));
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dlg), &rgba);
  gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(dlg), FALSE);
  if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK) {
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dlg), &rgba);
    int r = CLAMP((int)(rgba.red * 255.0 + 0.5), 0, 255);
    int g = CLAMP((int)(rgba.green * 255.0 + 0.5), 0, 255);
    int b = CLAMP((int)(rgba.blue * 255.0 + 0.5), 0, 255);
    guint32 packed = PACK_RGBA(r, g, b, 255);
    if (set_bg) {
      bg_color = packed;
      set_palette_rgb(0, (r << 16) | (g << 8) | b);
    } else {
      unsigned int rgb = (r << 16) | (g << 8) | b;
      double pr = r / 255.0, pg = g / 255.0, pb = b / 255.0;
      int found = -1;
      for (int i = 0; i < PALETTE_SIZE; i++) {
        if (palette[i][0] == pr && palette[i][1] == pg &&
            palette[i][2] == pb) {
          found = i;
          break;
        }
      }
      if (found < 0 && PALETTE_SIZE < 256 &&
          palette_reserve(palette_size + 1)) {
        found = palette_size;
        set_palette_rgb(found, rgb);
        palette_size++;
      }
      fg_color = packed;
    }
    gtk_widget_queue_draw(palette_bar);
    gtk_widget_queue_draw(main_canvas);
    status_update();
  }
  gtk_widget_destroy(dlg);
  cmd_set("");
}

static void exec_fliph(const char *arg) {
  (void)arg;
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
}

static void exec_flipv(const char *arg) {
  (void)arg;
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
}

static void exec_invert(const char *arg) {
  (void)arg;
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  begin_undo_action();
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      guint32 px = PX(y, x);
      guchar a = px & 0xff;
      if (a == 0)
        continue;
      guchar r = ~((px >> 24) & 0xff);
      guchar g = ~((px >> 16) & 0xff);
      guchar b = ~((px >> 8) & 0xff);
      push_undo(x, y);
      PX(y, x) = PACK_RGBA(r, g, b, a);
    }
  commit_undo_action();
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

static void exec_blur(const char *arg) {
  int radius = 1;
  if (*arg) {
    radius = atoi(arg);
    if (radius < 1 || radius > 64) {
      cmd_flash("Usage: :blur [N]  (N = 1..64)");
      return;
    }
  }
  int x0, y0, x1, y1;
  get_vis_rect(&x0, &y0, &x1, &y1);
  int w = x1 - x0 + 1, h = y1 - y0 + 1;
  guint32 *tmp = malloc((size_t)w * h * sizeof(guint32));
  if (!tmp) {
    cmd_flash("Out of memory.");
    return;
  }
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      long sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
      for (int dx = -radius; dx <= radius; dx++) {
        int sx = CLAMP(x0 + x + dx, x0, x1);
        guint32 px = PX(y0 + y, sx);
        sr += (px >> 24) & 0xff;
        sg += (px >> 16) & 0xff;
        sb += (px >> 8) & 0xff;
        sa += px & 0xff;
        cnt++;
      }
      tmp[y * w + x] = PACK_RGBA(sr / cnt, sg / cnt, sb / cnt, sa / cnt);
    }
  }
  begin_undo_action();
  for (int x = 0; x < w; x++) {
    for (int y = 0; y < h; y++) {
      long sr = 0, sg = 0, sb = 0, sa = 0, cnt = 0;
      for (int dy = -radius; dy <= radius; dy++) {
        int sy = CLAMP(y + dy, 0, h - 1);
        guint32 px = tmp[sy * w + x];
        sr += (px >> 24) & 0xff;
        sg += (px >> 16) & 0xff;
        sb += (px >> 8) & 0xff;
        sa += px & 0xff;
        cnt++;
      }
      push_undo(x0 + x, y0 + y);
      PX(y0 + y, x0 + x) = PACK_RGBA(sr / cnt, sg / cnt, sb / cnt, sa / cnt);
    }
  }
  free(tmp);
  commit_undo_action();
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

static void exec_stroke(const char *arg) {
  (void)arg;
  if (!visual_mode) {
    cmd_flash(":stroke requires a visual selection.");
    return;
  }
  int x0 = MIN(cursor_x, visual_anchor_x);
  int x1 = MAX(cursor_x, visual_anchor_x);
  int y0 = MIN(cursor_y, visual_anchor_y);
  int y1 = MAX(cursor_y, visual_anchor_y);
  begin_undo_action();
  for (int x = x0; x <= x1; x++) {
    paint_brush(x, y0, fg_color);
    paint_brush(x, y1, fg_color);
  }
  for (int y = y0 + 1; y < y1; y++) {
    paint_brush(x0, y, fg_color);
    paint_brush(x1, y, fg_color);
  }
  commit_undo_action();
  visual_mode = FALSE;
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

static void exec_rotate(const char *arg) {
  (void)arg;
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
}

static void exec_export(const char *arg) {
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

static void exec_center(const char *arg) {
  (void)arg;
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
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
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
}

static void exec_crop(const char *arg) {
  (void)arg;
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
  guint32 *before_snap =
      malloc((size_t)CANVAS_W * CANVAS_H * sizeof(guint32));
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
}

static void exec_text(const char *arg) {
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
  if (sw < 1)
    sw = 1;
  if (sh < 1)
    sh = 1;
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
}

static void exec_help(const char *arg) {
  (void)arg;
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
      "  E                   filled ellipse  (n=rx, :set ellipsery for ry)\n"
      "  e  (in visual mode) ellipse outline fitting selection\n"
      "  E  (in visual mode) filled ellipse fitting selection\n"
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
      "  + / -               grow / shrink selection by N pixels (n=count)\n"
      "  T                   copy selection to a new layer above active\n"
      "  :seltolay           same as T\n"
      "  :stroke             paint brush along border of selection\n"
      "\n"
      "Palette\n"
      "  c / C               cycle color forward / backward\n"
      "  Ctrl-1..9           select palette slot directly\n"
      "  e                   pick color under cursor\n"
      "  :set color <hex|name>          add/select color\n"
      "  :set color <idx> <hex|name>    edit palette slot\n"
      "  :set bg <hex|name>             set background color\n"
      "  :colorpicker  (:cp)            open HSL/RGB color picker dialog\n"
      "  :colorpicker bg  (:cp bg)      open color picker for background\n"
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
      "  :invert             invert RGB of canvas (or visual selection)\n"
      "  :blur [N]           box blur radius N (default 1) on canvas or "
      "selection\n"
      "  :newlayer              add transparent layer above active\n"
      "  :mergedown             composite active layer into layer below\n"
      "  :layer N               switch to layer N (1-based)\n"
      "  :layervis N            toggle visibility of layer N\n"
      "  :layerblend <mode>     set blend mode "
      "(normal|multiply|screen|overlay|...)\n"
      "  :layeropacity N        set active layer opacity 0-100\n"
      "  :guide h|v N           toggle horizontal/vertical guide at row/col "
      "N\n"
      "  :guide clear           remove all guides\n"
      "  :guide snap            toggle snap-to-guides in insert mode\n"
      "\n"
      "View\n"
      "  + / -               zoom in / out\n"
      "  | (pipe)            toggle grid\n"
      "  %                   toggle coordinate ruler\n"
      "  #                   toggle checkerboard background\n"
      "  :set onionskin / :set noonionskin  toggle onion skin overlay\n"
      "  :set onionskinopacity N    onion skin strength 1-100 (default 50)\n"
      "  :set gridcolor <hex|name>  set grid line colour\n"
      "  :set zoom N         set cell size\n"
      "  :set brush N        set brush size (1-16)\n"
      "  :set brushshape square|circle|custom  set brush shape\n"
      "  :brushdefine <pat>  define custom brush (rows sep by /, # = on)\n"
      "  :brushdefine        capture visual selection as custom brush\n"
      "  :set spray <1-100>|off  airbrush density (% pixels per stroke)\n"
      "  :set ellipsery <N>|0    y-radius for e/E (0 = same as rx)\n"
      "  :set fill solid|checker|hstripes|vstripes|halftone\n"
      "  :set sym h|v|hv|4|none  mirror symmetry\n"
      "  :set sym radial N       N-point radial symmetry (N=2..32)\n"
      "  :set undolevels N   set undo history depth (1-256, clears history)\n"
      "  Ctrl-G              show file info\n"
      "  :goto col,row       jump to position (1-based)\n"
      "  :find color <hex|name>  jump to nearest pixel of color\n"
      "  :replace <from> <to>    replace all pixels of one color with "
      "another\n"
      "  :gradtool [c1 c2]        interactive gradient (click-drag to "
      "apply)\n"
      "  :gradient <c1> <c2> h|v  fill canvas with gradient between two "
      "colors\n");
  gtk_dialog_run(GTK_DIALOG(dlg));
  gtk_widget_destroy(dlg);
  cmd_set("");
}

void cmd_execute(void) {
  const char *p = cmd_buf + 1;
  while (*p && *p != ' ')
    p++;
  while (*p == ' ')
    p++;
  const char *arg = p;

  if (strcmp(cmd_buf, ":q") == 0) {
    exec_quit(arg);
    return;
  }
  if (strcmp(cmd_buf, ":q!") == 0) {
    exec_force_quit(arg);
    return;
  }
  if (strncmp(cmd_buf, ":tabnew", 7) == 0) {
    exec_tabnew(arg);
    return;
  }
  if (strcmp(cmd_buf, ":new") == 0 || strcmp(cmd_buf, ":new!") == 0) {
    exec_new(arg);
    return;
  }
  if (strcmp(cmd_buf, ":w") == 0 || (strncmp(cmd_buf, ":w ", 3) == 0 && *arg)) {
    exec_write(arg);
    return;
  }
  if (strcmp(cmd_buf, ":wq") == 0 ||
      (strncmp(cmd_buf, ":wq ", 4) == 0 && *arg)) {
    exec_write_quit(arg);
    return;
  }
  if (strcmp(cmd_buf, ":e") == 0 || strcmp(cmd_buf, ":e!") == 0 ||
      (strncmp(cmd_buf, ":e ", 3) == 0 && *arg) ||
      (strncmp(cmd_buf, ":e! ", 4) == 0 && *arg)) {
    exec_edit(arg);
    return;
  }
  if (strncmp(cmd_buf, ":set ", 5) == 0) {
    exec_set(arg);
    return;
  }
  if (strncmp(cmd_buf, ":replace ", 9) == 0 && *arg) {
    exec_replace(arg);
    return;
  }
  if (strncmp(cmd_buf, ":gradient ", 10) == 0 && *arg) {
    exec_gradient(arg);
    return;
  }
  if (strncmp(cmd_buf, ":gradtool", 9) == 0) {
    exec_gradtool(arg);
    return;
  }
  if (strcmp(cmd_buf, ":brushdefine") == 0 ||
      strncmp(cmd_buf, ":brushdefine ", 13) == 0) {
    exec_brushdefine(arg);
    return;
  }
  if (strncmp(cmd_buf, ":hue ", 5) == 0 || strncmp(cmd_buf, ":sat ", 5) == 0 ||
      strncmp(cmd_buf, ":bright ", 8) == 0) {
    exec_hsl(arg);
    return;
  }
  if (strncmp(cmd_buf, ":dither ", 8) == 0 && *arg) {
    exec_dither(arg);
    return;
  }
  if (strncmp(cmd_buf, ":find color ", 12) == 0) {
    exec_find_color(arg);
    return;
  }
  if (strncmp(cmd_buf, ":goto ", 6) == 0 && *arg) {
    exec_goto(arg);
    return;
  }
  if (strncmp(cmd_buf, ":scale ", 7) == 0 && *arg) {
    exec_scale(arg);
    return;
  }
  if (strcmp(cmd_buf, ":newlayer") == 0) {
    exec_newlayer(arg);
    return;
  }
  if (strcmp(cmd_buf, ":seltolay") == 0) {
    exec_seltolay(arg);
    return;
  }
  if (strcmp(cmd_buf, ":mergedown") == 0) {
    exec_mergedown(arg);
    return;
  }
  if (strncmp(cmd_buf, ":layer ", 7) == 0 && *arg) {
    exec_layer(arg);
    return;
  }
  if (strncmp(cmd_buf, ":layervis ", 10) == 0 && *arg) {
    exec_layervis(arg);
    return;
  }
  if (strncmp(cmd_buf, ":layerblend ", 12) == 0 && *arg) {
    exec_layerblend(arg);
    return;
  }
  if (strncmp(cmd_buf, ":layeropacity ", 14) == 0 && *arg) {
    exec_layeropacity(arg);
    return;
  }
  if (strncmp(cmd_buf, ":guide ", 7) == 0 && *arg) {
    exec_guide(arg);
    return;
  }
  if (strncmp(cmd_buf, ":resize ", 8) == 0 && *arg) {
    exec_resize(arg);
    return;
  }
  if (strncmp(cmd_buf, ":savep ", 7) == 0 && *arg) {
    exec_savep(arg);
    return;
  }
  if (strncmp(cmd_buf, ":loadp ", 7) == 0 && *arg) {
    exec_loadp(arg);
    return;
  }
  if (strncmp(cmd_buf, ":importp ", 9) == 0 && *arg) {
    exec_importp(arg);
    return;
  }
  if (strncmp(cmd_buf, ":delp ", 6) == 0 && *arg) {
    exec_delp(arg);
    return;
  }
  if (strcmp(cmd_buf, ":colorpicker") == 0 || strcmp(cmd_buf, ":cp") == 0 ||
      strcmp(cmd_buf, ":colorpicker bg") == 0 ||
      strcmp(cmd_buf, ":cp bg") == 0) {
    exec_colorpicker(arg);
    return;
  }
  if (strcmp(cmd_buf, ":fliph") == 0) {
    exec_fliph(arg);
    return;
  }
  if (strcmp(cmd_buf, ":flipv") == 0) {
    exec_flipv(arg);
    return;
  }
  if (strcmp(cmd_buf, ":invert") == 0) {
    exec_invert(arg);
    return;
  }
  if (strcmp(cmd_buf, ":blur") == 0 || strncmp(cmd_buf, ":blur ", 6) == 0) {
    exec_blur(arg);
    return;
  }
  if (strcmp(cmd_buf, ":stroke") == 0) {
    exec_stroke(arg);
    return;
  }
  if (strcmp(cmd_buf, ":rotate") == 0) {
    exec_rotate(arg);
    return;
  }
  if (strncmp(cmd_buf, ":export ", 8) == 0 && *arg) {
    exec_export(arg);
    return;
  }
  if (strcmp(cmd_buf, ":center") == 0) {
    exec_center(arg);
    return;
  }
  if (strcmp(cmd_buf, ":crop") == 0) {
    exec_crop(arg);
    return;
  }
  if (strncmp(cmd_buf, ":text ", 6) == 0 && *arg) {
    exec_text(arg);
    return;
  }
  if (strcmp(cmd_buf, ":help") == 0) {
    exec_help(arg);
    return;
  }

  cmd_flash("Unknown command.");
}
