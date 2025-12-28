#include "vimpaint.h"

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

void rgb_to_hsl(double r, double g, double b, double *h, double *s,
                double *l) {
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

void hsl_to_rgb(double h, double s, double l, double *r, double *g,
                double *b) {
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
