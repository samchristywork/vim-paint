#include "palette.h"
#include "main.h"

int palette_reserve(int needed) {
  if (needed <= palette_cap)
    return 1;
  int nc = palette_cap ? palette_cap * 2 : 8;
  while (nc < needed)
    nc *= 2;
  double (*np)[3] = realloc(palette, (size_t)nc * sizeof(*np));
  if (!np)
    return 0;
  palette = np;
  palette_cap = nc;
  return 1;
}

// clang-format off
const NamedColor named_colors[] = {
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
// clang-format on
int named_colors_count = (int)(sizeof named_colors / sizeof named_colors[0]);

/* Returns TRUE and sets *out_rgb on success.
   Flashes "Invalid hex color." and returns FALSE if val starts with '#' but is
   malformed. Returns FALSE without flashing if val is neither a valid hex nor a
   known name. */
void palette_to_rgb(int idx, int *r, int *g, int *b) {
  if (idx < 0 || idx >= PALETTE_SIZE) {
    *r = *g = *b = 0;
    return;
  }
  *r = (int)(palette[idx][0] * 255 + 0.5);
  *g = (int)(palette[idx][1] * 255 + 0.5);
  *b = (int)(palette[idx][2] * 255 + 0.5);
}

void set_palette_rgb(int slot, unsigned int rgb) {
  palette[slot][0] = ((rgb >> 16) & 0xff) / 255.0;
  palette[slot][1] = ((rgb >> 8) & 0xff) / 255.0;
  palette[slot][2] = (rgb & 0xff) / 255.0;
}

void rgb_to_hsl(double r, double g, double b, double *h, double *s, double *l) {
  double mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
  double mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
  *l = (mx + mn) / 2.0;
  if (mx == mn) {
    *h = *s = 0.0;
    return;
  }
  double d = mx - mn;
  *s = *l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
  if (mx == r)
    *h = (g - b) / d + (g < b ? 6.0 : 0.0);
  else if (mx == g)
    *h = (b - r) / d + 2.0;
  else
    *h = (r - g) / d + 4.0;
  *h *= 60.0;
}

double hue_to_rgb(double p, double q, double t) {
  if (t < 0)
    t += 1;
  if (t > 1)
    t -= 1;
  if (t < 1.0 / 6)
    return p + (q - p) * 6 * t;
  if (t < 0.5)
    return q;
  if (t < 2.0 / 3)
    return p + (q - p) * (2.0 / 3 - t) * 6;
  return p;
}

void hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b) {
  if (s == 0.0) {
    *r = *g = *b = l;
    return;
  }
  h /= 360.0;
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  double p = 2 * l - q;
  *r = hue_to_rgb(p, q, h + 1.0 / 3);
  *g = hue_to_rgb(p, q, h);
  *b = hue_to_rgb(p, q, h - 1.0 / 3);
}

gboolean parse_color(const char *val, unsigned int *out_rgb) {
  if (val[0] == '#') {
    size_t len = strlen(val);
    if (len == 4) {
      unsigned int r, g, b;
      if (sscanf(val + 1, "%1x%1x%1x", &r, &g, &b) == 3) {
        *out_rgb = ((r * 17) << 16) | ((g * 17) << 8) | (b * 17);
        return TRUE;
      }
    } else if (len >= 7) {
      char tmp[8];
      strncpy(tmp, val + 1, 6);
      tmp[6] = '\0';
      if (sscanf(tmp, "%06x", out_rgb) == 1)
        return TRUE;
    }
    cmd_flash("Invalid hex color.");
    return FALSE;
  }
  for (int i = 0; i < NAMED_COLORS_COUNT; i++) {
    if (strcasecmp(val, named_colors[i].name) == 0) {
      *out_rgb = named_colors[i].rgb;
      return TRUE;
    }
  }
  return FALSE;
}

void delp(const char *arg) {
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

void colorpicker(const char *arg) {
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
        if (palette[i][0] == pr && palette[i][1] == pg && palette[i][2] == pb) {
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

void savep(const char *arg) {
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

void loadp(const char *arg) {
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

void importp(const char *arg) {
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

gboolean exec_palette(const char *cmd, const char *arg) {
  if (strncmp(cmd, ":savep ", 7) == 0 && *arg) {
    savep(arg);
    return TRUE;
  }
  if (strncmp(cmd, ":loadp ", 7) == 0 && *arg) {
    loadp(arg);
    return TRUE;
  }
  if (strncmp(cmd, ":importp ", 9) == 0 && *arg) {
    importp(arg);
    return TRUE;
  }
  if (strncmp(cmd, ":delp ", 6) == 0 && *arg) {
    delp(arg);
    return TRUE;
  }
  if (strcmp(cmd, ":colorpicker") == 0 || strcmp(cmd, ":cp") == 0 ||
      strcmp(cmd, ":colorpicker bg") == 0 || strcmp(cmd, ":cp bg") == 0) {
    colorpicker(arg);
    return TRUE;
  }
  return FALSE;
}
