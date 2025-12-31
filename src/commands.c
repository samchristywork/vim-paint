#include "commands.h"
#include "canvas.h"
#include "fileio.h"
#include "layers.h"
#include "main.h"
#include "palette.h"
#include "undo.h"

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
