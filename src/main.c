#include "main.h"
#include "io.h"
#include "keys.h"
#include "layers.h"
#include "mouse.h"
#include "palette.h"
#include "render.h"
#include "undo.h"

void zoom_resize(void) {
  gtk_widget_set_size_request(main_canvas, CANVAS_W * CELL_SIZE,
                              CANVAS_H * CELL_SIZE);
  gtk_widget_set_size_request(palette_bar, CANVAS_W * CELL_SIZE, SWATCH_H);
  gtk_widget_set_size_request(cmd_label, CANVAS_W * CELL_SIZE, 20);
  gtk_window_resize(GTK_WINDOW(main_window), 1, 1);
}


void flash_color(int idx) {
  char buf[64];
  int r, g, b;
  palette_to_rgb(idx, &r, &g, &b);
  snprintf(buf, sizeof(buf), "color %d  #%02x%02x%02x", idx, r, g, b);
  cmd_flash(buf);
}

void update_title(const char *filename) {
  char buf[4300];
  snprintf(buf, sizeof(buf), "vim-paint - %s%s  %dx%d  (%d,%d)", filename,
           canvas_dirty ? " [+]" : "", CANVAS_W, CANVAS_H, cursor_x + 1,
           cursor_y + 1);
  gtk_window_set_title(GTK_WINDOW(main_window), buf);
}

void title_refresh(void) {
  char buf[512];
  char tab_info[20] = "";
  if (tab_count > 1)
    snprintf(tab_info, sizeof(tab_info), "[%d/%d] ", tab_current + 1,
             tab_count);
  if (*last_filename)
    snprintf(buf, sizeof(buf), "vim-paint %s- %s%s  %dx%d  (%d,%d)", tab_info,
             last_filename, canvas_dirty ? " [+]" : "", CANVAS_W, CANVAS_H,
             cursor_x + 1, cursor_y + 1);
  else
    snprintf(buf, sizeof(buf), "vim-paint %s%s  %dx%d  (%d,%d)", tab_info,
             canvas_dirty ? "[+] " : "", CANVAS_W, CANVAS_H, cursor_x + 1,
             cursor_y + 1);
  gtk_window_set_title(GTK_WINDOW(main_window), buf);
}

void tab_save(int idx) {
  layers_flatten();
  size_t sz = (size_t)(size_t)CANVAS_W * CANVAS_H * sizeof(guint32);
  if (!tabs[idx].pixels || tabs[idx].w != CANVAS_W || tabs[idx].h != CANVAS_H) {
    free(tabs[idx].pixels);
    tabs[idx].pixels = malloc(sz);
    if (!tabs[idx].pixels)
      return;
  }
  memcpy(tabs[idx].pixels, pixels, sz);
  tabs[idx].w = CANVAS_W;
  tabs[idx].h = CANVAS_H;
  tabs[idx].cx = cursor_x;
  tabs[idx].cy = cursor_y;
  tabs[idx].dirty = canvas_dirty;
  snprintf(tabs[idx].filename, sizeof(tabs[idx].filename), "%s", last_filename);
  /* Transfer undo/redo ownership from globals to tab storage */
  memcpy(tabs[idx].undo_stack, undo_stack, sizeof(undo_stack));
  tabs[idx].undo_top = undo_top;
  tabs[idx].undo_count = undo_count;
  memcpy(tabs[idx].redo_stack, redo_stack, sizeof(redo_stack));
  tabs[idx].redo_top = redo_top;
  tabs[idx].redo_count = redo_count;
  memset(undo_stack, 0, sizeof(undo_stack));
  undo_top = 0;
  undo_count = 0;
  memset(redo_stack, 0, sizeof(redo_stack));
  redo_top = 0;
  redo_count = 0;
  staged_count = 0;
}

void tab_switch(int newidx) {
  if (newidx < 0 || newidx >= tab_count || newidx == tab_current)
    return;
  macro_recording = FALSE;
  tab_save(tab_current);
  tab_current = newidx;
  TabState *t = &tabs[tab_current];
  guint32 *np = malloc(t->w * t->h * sizeof(guint32));
  if (!np)
    return;
  memcpy(np, t->pixels, t->w * t->h * sizeof(guint32));
  free(pixels);
  pixels = np;
  layer_bufs[0] = pixels;
  layer_count = 1;
  layer_active = 0;
  layer_visible[0] = TRUE;
  layer_opacity[0] = 100;
  layer_blend[0] = BLEND_NORMAL;
  snprintf(layer_name[0], 32, "Layer 1");
  CANVAS_W = t->w;
  CANVAS_H = t->h;
  cursor_x = CLAMP(t->cx, 0, CANVAS_W - 1);
  cursor_y = CLAMP(t->cy, 0, CANVAS_H - 1);
  canvas_dirty = t->dirty;
  visual_mode = FALSE;
  insert_mode = FALSE;
  snprintf(last_filename, sizeof(last_filename), "%s", t->filename);
  /* Restore undo/redo ownership from tab storage to globals */
  memcpy(undo_stack, t->undo_stack, sizeof(undo_stack));
  undo_top = t->undo_top;
  undo_count = t->undo_count;
  memcpy(redo_stack, t->redo_stack, sizeof(redo_stack));
  redo_top = t->redo_top;
  redo_count = t->redo_count;
  memset(t->undo_stack, 0, sizeof(t->undo_stack));
  t->undo_top = t->undo_count = 0;
  memset(t->redo_stack, 0, sizeof(t->redo_stack));
  t->redo_top = t->redo_count = 0;
  staged_count = 0;
  zoom_resize();
  title_refresh();
  status_update();
  gtk_widget_queue_draw(main_canvas);
}

void tab_close_current(void) {
  /* Free the closing tab's undo history (currently in globals) */
  clear_history();
  free(tabs[tab_current].pixels);
  tabs[tab_current].pixels = NULL;
  /* Shift remaining tabs down; zero the vacated trailing slot's undo pointers
     to avoid aliased ownership after the struct copy. */
  for (int i = tab_current; i < tab_count - 1; i++)
    tabs[i] = tabs[i + 1];
  memset(tabs[tab_count - 1].undo_stack, 0, sizeof(tabs[0].undo_stack));
  tabs[tab_count - 1].undo_top = tabs[tab_count - 1].undo_count = 0;
  memset(tabs[tab_count - 1].redo_stack, 0, sizeof(tabs[0].redo_stack));
  tabs[tab_count - 1].redo_top = tabs[tab_count - 1].redo_count = 0;
  tabs[tab_count - 1].pixels = NULL;
  tab_count--;
  if (tab_current >= tab_count)
    tab_current = tab_count - 1;
  TabState *t = &tabs[tab_current];
  guint32 *np = malloc(t->w * t->h * sizeof(guint32));
  if (!np)
    return;
  memcpy(np, t->pixels, t->w * t->h * sizeof(guint32));
  free(pixels);
  pixels = np;
  layer_bufs[0] = pixels;
  layer_count = 1;
  layer_active = 0;
  layer_visible[0] = TRUE;
  layer_opacity[0] = 100;
  layer_blend[0] = BLEND_NORMAL;
  snprintf(layer_name[0], 32, "Layer 1");
  CANVAS_W = t->w;
  CANVAS_H = t->h;
  cursor_x = CLAMP(t->cx, 0, CANVAS_W - 1);
  cursor_y = CLAMP(t->cy, 0, CANVAS_H - 1);
  canvas_dirty = t->dirty;
  visual_mode = FALSE;
  insert_mode = FALSE;
  snprintf(last_filename, sizeof(last_filename), "%s", t->filename);
  /* Restore the new current tab's undo/redo history */
  memcpy(undo_stack, t->undo_stack, sizeof(undo_stack));
  undo_top = t->undo_top;
  undo_count = t->undo_count;
  memcpy(redo_stack, t->redo_stack, sizeof(redo_stack));
  redo_top = t->redo_top;
  redo_count = t->redo_count;
  memset(t->undo_stack, 0, sizeof(t->undo_stack));
  t->undo_top = t->undo_count = 0;
  memset(t->redo_stack, 0, sizeof(t->redo_stack));
  t->redo_top = t->redo_count = 0;
  staged_count = 0;
  zoom_resize();
  title_refresh();
  status_update();
  gtk_widget_queue_draw(main_canvas);
  cmd_set("");
}

void cmd_set(const char *text) {
  gtk_label_set_text(GTK_LABEL(cmd_label), text);
}

void status_update(void) {
  if (cmd_mode)
    return;
  if (flash_timer_id)
    return;
  char buf[160];
  const char *mode = gradient_tool ? "GRADIENT"
                     : visual_mode ? "VISUAL"
                     : insert_mode ? "INSERT"
                                   : "NORMAL";
  guchar r = (fg_color >> 24) & 0xff;
  guchar g = (fg_color >> 16) & 0xff;
  guchar b = (fg_color >> 8) & 0xff;
  char layer_info[64] = "";
  if (layer_count > 1) {
    BlendMode bm = layer_blend[layer_active];
    int op = layer_opacity[layer_active];
    char bm_part[24] = "", op_part[12] = "";
    if (bm != BLEND_NORMAL)
      snprintf(bm_part, sizeof(bm_part), "%s", blend_mode_names[bm]);
    if (op != 100)
      snprintf(op_part, sizeof(op_part), "%d%%", op);
    if (bm_part[0] && op_part[0])
      snprintf(layer_info, sizeof(layer_info), "  L%d/%d[%s@%s]",
               layer_active + 1, layer_count, bm_part, op_part);
    else if (bm_part[0])
      snprintf(layer_info, sizeof(layer_info), "  L%d/%d[%s]", layer_active + 1,
               layer_count, bm_part);
    else if (op_part[0])
      snprintf(layer_info, sizeof(layer_info), "  L%d/%d[%s]", layer_active + 1,
               layer_count, op_part);
    else
      snprintf(layer_info, sizeof(layer_info), "  L%d/%d", layer_active + 1,
               layer_count);
  }
  char sym_info[24] = "";
  switch (sym_mode) {
  case SYM_H:
    snprintf(sym_info, sizeof(sym_info), "  symH");
    break;
  case SYM_V:
    snprintf(sym_info, sizeof(sym_info), "  symV");
    break;
  case SYM_HV:
    snprintf(sym_info, sizeof(sym_info), "  sym4");
    break;
  case SYM_RADIAL:
    snprintf(sym_info, sizeof(sym_info), "  sym%d", sym_radial_n);
    break;
  default:
    break;
  }
  if (macro_recording)
    snprintf(
        buf, sizeof(buf),
        " %s  recording @%c  col: %d  row: %d  #%02x%02x%02x  %dx%d  z%d%s%s",
        mode, 'a' + macro_reg, cursor_x + 1, cursor_y + 1, r, g, b, CANVAS_W,
        CANVAS_H, CELL_SIZE, layer_info, sym_info);
  else
    snprintf(buf, sizeof(buf),
             " %s  col: %d  row: %d  #%02x%02x%02x  %dx%d  z%d%s%s", mode,
             cursor_x + 1, cursor_y + 1, r, g, b, CANVAS_W, CANVAS_H, CELL_SIZE,
             layer_info, sym_info);
  gtk_label_set_text(GTK_LABEL(cmd_label), buf);
  title_refresh();
}

gboolean on_flash_expire(gpointer data) {
  flash_timer_id = 0;
  status_update();
  return G_SOURCE_REMOVE;
}

void cmd_flash(const char *text) {
  cmd_set(text);
  if (flash_timer_id)
    g_source_remove(flash_timer_id);
  flash_timer_id = g_timeout_add(2000, on_flash_expire, NULL);
}

void usage(const char *prog, int exitcode) {
  FILE *out = exitcode ? stderr : stdout;
  fprintf(out,
          "Usage: %s [OPTIONS] [FILE]\n"
          "\n"
          "A vim-inspired pixel art editor.\n"
          "\n"
          "Arguments:\n"
          "  FILE        PNG file to open at startup\n"
          "\n"
          "Options:\n"
          "  -W N        Canvas width in cells (default: %d)\n"
          "  -H N        Canvas height in cells (default: %d)\n"
          "  -z N        Cell size in pixels, 4-64 (default: %d)\n"
          "  -h          Show this help message\n",
          prog, CANVAS_W, CANVAS_H, CELL_SIZE);
  exit(exitcode);
}

int main(int argc, char *argv[]) {
  int explicit_w = 0, explicit_h = 0;
  int opt;
  while ((opt = getopt(argc, argv, "W:H:z:h")) != -1) {
    switch (opt) {
    case 'W': {
      int v = atoi(optarg);
      if (v > 0 && v <= 16384) {
        CANVAS_W = v;
        explicit_w = 1;
      } else if (v > 16384) {
        fprintf(stderr, "Warning: -W %d clamped to 16384\n", v);
        CANVAS_W = 16384;
        explicit_w = 1;
      }
      break;
    }
    case 'H': {
      int v = atoi(optarg);
      if (v > 0 && v <= 16384) {
        CANVAS_H = v;
        explicit_h = 1;
      } else if (v > 16384) {
        fprintf(stderr, "Warning: -H %d clamped to 16384\n", v);
        CANVAS_H = 16384;
        explicit_h = 1;
      }
      break;
    }
    case 'z': {
      int v = atoi(optarg);
      if (v > 0)
        CELL_SIZE = CLAMP(v, 4, 64);
      break;
    }
    case 'h':
      usage(argv[0], 0);
      break;
    default:
      usage(argv[0], 1);
    }
  }

  char **startup_files = (optind < argc) ? &argv[optind] : NULL;
  int startup_count = argc - optind;

  /* Size the initial canvas to match the first file if it is a valid PNG. */
  if (startup_count > 0) {
    cairo_surface_t *probe =
        cairo_image_surface_create_from_png(startup_files[0]);
    if (cairo_surface_status(probe) == CAIRO_STATUS_SUCCESS) {
      if (!explicit_w)
        CANVAS_W = cairo_image_surface_get_width(probe);
      if (!explicit_h)
        CANVAS_H = cairo_image_surface_get_height(probe);
    }
    cairo_surface_destroy(probe);
  }

  static const double default_pal[8][3] = {
      {1.0, 1.0, 1.0}, {0.0, 0.0, 0.0}, {0.8, 0.1, 0.1}, {0.1, 0.7, 0.1},
      {0.1, 0.3, 0.9}, {0.9, 0.8, 0.0}, {0.0, 0.7, 0.8}, {0.7, 0.0, 0.8},
  };
  palette_reserve(8);
  memcpy(palette, default_pal, sizeof(default_pal));
  palette_size = 8;

  pixels = calloc((size_t)CANVAS_W * CANVAS_H, sizeof(guint32));
  layer_bufs[0] = pixels;
  layer_visible[0] = TRUE;
  layer_opacity[0] = 100;
  layer_blend[0] = BLEND_NORMAL;
  snprintf(layer_name[0], 32, "Layer 1");
  layer_count = 1;
  layer_active = 0;

  /* Pass only program name + positional args to gtk_init */
  char **gtk_argv = argv + optind - 1;
  gtk_argv[0] = argv[0];
  int gtk_argc = argc - optind + 1;
  gtk_init(&gtk_argc, &gtk_argv);

  main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  GtkWidget *window = main_window;
  gtk_window_set_title(GTK_WINDOW(window), "vim-paint");
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window), vbox);

  main_canvas = gtk_drawing_area_new();
  gtk_widget_set_size_request(main_canvas, CANVAS_W * CELL_SIZE,
                              CANVAS_H * CELL_SIZE);
  gtk_box_pack_start(GTK_BOX(vbox), main_canvas, FALSE, FALSE, 0);

  palette_bar = gtk_drawing_area_new();
  gtk_widget_set_size_request(palette_bar, CANVAS_W * CELL_SIZE, SWATCH_H);
  gtk_box_pack_start(GTK_BOX(vbox), palette_bar, FALSE, FALSE, 0);

  cmd_label = gtk_label_new("");
  gtk_label_set_xalign(GTK_LABEL(cmd_label), 0.0);
  gtk_widget_set_size_request(cmd_label, CANVAS_W * CELL_SIZE, 20);
  gtk_box_pack_start(GTK_BOX(vbox), cmd_label, FALSE, FALSE, 0);

  gtk_widget_add_events(main_canvas,
                        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                            GDK_BUTTON1_MOTION_MASK | GDK_BUTTON3_MOTION_MASK |
                            GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
  gtk_widget_add_events(palette_bar, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(main_canvas, "draw", G_CALLBACK(on_draw), NULL);
  g_signal_connect(main_canvas, "button-press-event",
                   G_CALLBACK(on_button_press), NULL);
  g_signal_connect(main_canvas, "button-release-event",
                   G_CALLBACK(on_button_release), NULL);
  g_signal_connect(main_canvas, "motion-notify-event",
                   G_CALLBACK(on_motion_notify), NULL);
  g_signal_connect(main_canvas, "scroll-event", G_CALLBACK(on_scroll), NULL);
  g_signal_connect(palette_bar, "draw", G_CALLBACK(on_palette_draw), NULL);
  g_signal_connect(palette_bar, "button-press-event",
                   G_CALLBACK(on_palette_click), NULL);
  g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press),
                   main_canvas);

  gtk_widget_show_all(window);
  status_update();

  if (startup_count > 0) {
    cmd_open(startup_files[0]);
    for (int i = 1; i < startup_count && tab_count < TAB_MAX; i++) {
      tab_save(tab_current);
      guint32 *np =
          calloc(DEFAULT_CANVAS_W * DEFAULT_CANVAS_H, sizeof(guint32));
      if (!np)
        break;
      free(pixels);
      pixels = np;
      CANVAS_W = DEFAULT_CANVAS_W;
      CANVAS_H = DEFAULT_CANVAS_H;
      cursor_x = 0;
      cursor_y = 0;
      canvas_dirty = FALSE;
      visual_mode = FALSE;
      insert_mode = FALSE;
      last_filename[0] = '\0';
      tab_count++;
      tab_current = tab_count - 1;
      tab_save(tab_current);
      cmd_open(startup_files[i]);
    }
    if (tab_count > 1)
      tab_switch(0);
  }

  gtk_main();

  return 0;
}

void app_quit(const char *arg) {
  (void)arg;
  if (canvas_dirty)
    cmd_flash(
        "Unsaved changes. Use :q! to force quit or :wq to save and quit.");
  else if (tab_count > 1)
    tab_close_current();
  else
    gtk_main_quit();
}

void force_quit(const char *arg) {
  (void)arg;
  if (tab_count > 1)
    tab_close_current();
  else
    gtk_main_quit();
}

void tabnew(const char *arg) {
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

void new_canvas(const char *arg) {
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

gboolean exec_app(const char *cmd, const char *arg) {
  if (strcmp(cmd, ":q") == 0) {
    app_quit(arg);
    return TRUE;
  }
  if (strcmp(cmd, ":q!") == 0) {
    force_quit(arg);
    return TRUE;
  }
  if (strncmp(cmd, ":tabnew", 7) == 0) {
    tabnew(arg);
    return TRUE;
  }
  if (strcmp(cmd, ":new") == 0 || strcmp(cmd, ":new!") == 0) {
    new_canvas(arg);
    return TRUE;
  }
  return FALSE;
}
