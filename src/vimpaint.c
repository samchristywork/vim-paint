#include <gtk/gtk.h>

/*
 * Globals
 */
GtkWidget *box;
GtkWidget *drawingArea;
GtkWidget *mainWindow;
cairo_surface_t *backgroundImage;
double fontSize = 20;
double offsetX = 0;
double offsetY = 0;
double zoom = .4;
int currentLayer = 0;
int cursorPositionX = 0;
int cursorPositionY = 0;
int pixelSize = 100;
int shiftMultiplier = 5;
char *currentFile = "res/test.png";

enum { MODE_NORMAL, MODE_COLOR_SELECTION };
int mode = MODE_COLOR_SELECTION;

struct color {
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char a;
};

struct color *palette[9];

struct layer {
  unsigned char *pixels;
  cairo_surface_t *surface;
  cairo_t *cr;
  int width;
  int height;
};

struct layer layers[9];

struct color *newColor(unsigned char r, unsigned char g, unsigned char b,
                       unsigned char a) {
  struct color *c = malloc(sizeof(struct color));
  c->r = r;
  c->g = g;
  c->b = b;
  c->a = a;
  return c;
}

/*
 * Set a pixel at a location on the specified layer to some color
 */
int setPixel(struct layer layer, int x, int y, int r, int g, int b, int a) {
  if (x < 0 || y < 0 || x > layer.width || y > layer.width) {
    return -1;
  }
  int index = (cursorPositionX + cursorPositionY * layer.width) * 4;
  layer.pixels[index + 0] = r;
  layer.pixels[index + 1] = g;
  layer.pixels[index + 2] = b;
  layer.pixels[index + 3] = a;

  return 0;
}

/*
 * Clear a pixel on the buffer
 */
int clearPixel(struct layer layer, int x, int y) {
  return setPixel(layer, x, y, 0, 0, 0, 0);
}

/*
 * Move the cursor with bounds detection
 */

void moveCursor(int dx, int dy) {
  cursorPositionX += dx;
  cursorPositionY += dy;
  if (cursorPositionX < 0) {
    cursorPositionX = 0;
  }
  if (cursorPositionY < 0) {
    cursorPositionY = 0;
  }
  if (cursorPositionX > layers[currentLayer].width) {
    cursorPositionX = layers[currentLayer].width;
  }
  if (cursorPositionY > layers[currentLayer].height) {
    cursorPositionY = layers[currentLayer].height;
  }
}

/*
 * Callback for keyboard events
 */
gboolean keyPressCallback(GtkWidget *widget, GdkEventKey *event,
                          gpointer data) {

  /*
   * Exit when the user presses Escape
   */
  if (event->keyval == GDK_KEY_Escape) {
    if (mode == MODE_COLOR_SELECTION) {
      mode = MODE_NORMAL;
    } else {
      exit(EXIT_SUCCESS);
      return TRUE;
    }
  }

  else if (event->keyval == GDK_KEY_minus) {
    zoom /= 1.1;
  } else if (event->keyval == GDK_KEY_plus) {
    zoom *= 1.1;
  }

  else if (event->keyval == GDK_KEY_space) {
    setPixel(layers[currentLayer], cursorPositionX, cursorPositionY, 255, 0, 0,
             255);
  }

  else if (event->keyval == GDK_KEY_h) {
    moveCursor(-1, 0);
  } else if (event->keyval == GDK_KEY_j) {
    moveCursor(0, 1);
  } else if (event->keyval == GDK_KEY_k) {
    moveCursor(0, -1);
  } else if (event->keyval == GDK_KEY_l) {
    moveCursor(1, 0);
  }

  else if (event->keyval == GDK_KEY_H) {
    moveCursor(-shiftMultiplier, 0);
  } else if (event->keyval == GDK_KEY_J) {
    moveCursor(0, shiftMultiplier);
  } else if (event->keyval == GDK_KEY_K) {
    moveCursor(0, -shiftMultiplier);
  } else if (event->keyval == GDK_KEY_L) {
    moveCursor(shiftMultiplier, 0);
  }

  else {
    printf("%d\n", event->keyval);
    return FALSE;
  }

  gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
  return TRUE;
}

/*
 * Called every time a redraw is requested for the drawing area
 */
gboolean drawCallback(GtkWidget *widget, cairo_t *cr, gpointer data) {

  /*
   * In case we want to use style information
   */
  GtkStyleContext *context = gtk_widget_get_style_context(widget);
  GdkRGBA color;
  gtk_style_context_get_color(context, gtk_style_context_get_state(context),
                              &color);

  /*
   * Get dimensions so we know how much room we have to draw
   */
  guint width = gtk_widget_get_allocated_width(box);
  guint height = gtk_widget_get_allocated_height(widget);
  gtk_render_background(context, cr, 0, 0, width, height);

  /*
   * Draw background image
   */
  cairo_save(cr);
  cairo_translate(cr, offsetX, offsetY);
  cairo_scale(cr, zoom, zoom);
  cairo_set_source_surface(cr, backgroundImage, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);

  /*
   * Draw all of the pixels for the given layer
   */
  for (int i = 0; i < 9; i++) {
    for (int x = 0; x < layers[i].width; x++) {
      for (int y = 0; y < layers[i].height; y++) {
        int index = (x + y * layers[i].width) * 4;
        double r = layers[i].pixels[index + 0] / 255.;
        double g = layers[i].pixels[index + 1] / 255.;
        double b = layers[i].pixels[index + 2] / 255.;
        unsigned char a = layers[i].pixels[index + 3];
        if (a) {
          cairo_save(layers[i].cr);
          cairo_translate(layers[i].cr, x * pixelSize * zoom,
                          y * pixelSize * zoom);
          cairo_rectangle(layers[i].cr, 0, 0, pixelSize * zoom,
                          pixelSize * zoom);
          cairo_set_source_rgba(layers[i].cr, r, g, b, 1);
          cairo_fill(layers[i].cr);
          cairo_restore(layers[i].cr);
        }
      }
    }
  }

  /*
   * Composite all the layers together
   */
  for (int i = 0; i < 9; i++) {
    cairo_set_source_surface(cr, layers[i].surface, 0, 0);
    cairo_paint(cr);
  }

  /*
   * Draw statusline
   */
  char buf[256];
  snprintf(buf, 255, "%s, (%d, %d), %f", currentFile, cursorPositionX,
           cursorPositionY, zoom);
  cairo_move_to(cr, 10, height - 10);
  cairo_set_font_size(cr, fontSize);
  cairo_set_source_rgba(cr, 1, 0, 0, 1);
  cairo_show_text(cr, buf);
  cairo_fill(cr);

  /*
   * Draw cursor
   */
  cairo_save(cr);
  cairo_set_source_rgba(cr, 1, 0, 0, 1);
  cairo_translate(cr, cursorPositionX * pixelSize * zoom,
                  cursorPositionY * pixelSize * zoom);
  cairo_rectangle(cr, 0, 0, pixelSize * zoom, pixelSize * zoom);
  cairo_set_line_width(cr, 2);
  cairo_stroke(cr);
  cairo_restore(cr);

  /*
   * Draw menu
   */
  if (mode == MODE_COLOR_SELECTION) {
    cairo_set_source_rgba(cr, .1, .1, .1, 1);
    cairo_rectangle(cr, width / 2 - 100, height / 2 - 50, 200, 100);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, .25, .25, .5, 1);
    cairo_rectangle(cr, width / 2 - 100, height / 2 - 50, 200, 100);
    cairo_stroke(cr);
    for (int x = 0; x < 3; x++) {
      for (int y = 0; y < 3; y++) {
        cairo_set_source_rgba(cr, (float)palette[x + 3 * y]->r / 255.,
                              (float)palette[x + 3 * y]->g / 255.,
                              (float)palette[x + 3 * y]->b / 255., 1);
        int xp = width / 2 + (x - 1) * 50 - 25;
        int yp = height / 2 + (y - 1) * 25 - 12;
        cairo_rectangle(cr, xp, yp, 50, 25);
        cairo_fill(cr);

        char buf[256];
        snprintf(buf, 255, "%d", x + y * 3 + 1);
        cairo_set_font_size(cr, 20);
        cairo_text_extents_t extents;
        cairo_text_extents(cr, buf, &extents);
        cairo_move_to(cr, xp - extents.width / 2 + 25, yp + 20);
        cairo_set_source_rgba(cr, 1. - (float)palette[x + 3 * y]->r / 255.,
                              1. - (float)palette[x + 3 * y]->g / 255.,
                              1. - (float)palette[x + 3 * y]->b / 255., 1);
        cairo_show_text(cr, buf);
        cairo_fill(cr);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  gtk_init(&argc, &argv);

  for (int i = 0; i < 9; i++) {
    layers[i].width = 100;
    layers[i].height = 100;
    layers[i].surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, layers[i].width * pixelSize,
        layers[i].height * pixelSize);
    layers[i].cr = cairo_create(layers[i].surface);
    layers[i].pixels = malloc(layers[i].width * layers[i].height * 4);
    bzero(layers[i].pixels, layers[i].width * layers[i].height * 4);
  }

  /*
   * Default color palette
   */
  palette[0] = newColor(0, 0, 0, 255);
  palette[1] = newColor(255, 0, 0, 255);
  palette[2] = newColor(0, 255, 0, 255);
  palette[3] = newColor(0, 0, 255, 255);
  palette[4] = newColor(255, 255, 0, 255);
  palette[5] = newColor(0, 255, 255, 255);
  palette[6] = newColor(255, 0, 255, 255);
  palette[7] = newColor(255, 255, 255, 255);
  palette[8] = newColor(0, 0, 0, 255);

  /*
   * Initialize the widgets and load the background image
   */
  mainWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  backgroundImage = cairo_image_surface_create_from_png(currentFile);
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  drawingArea = gtk_drawing_area_new();

  /*
   * Handle events
   */
  g_signal_connect(G_OBJECT(drawingArea), "draw", G_CALLBACK(drawCallback),
                   NULL);
  gtk_widget_add_events(mainWindow, GDK_KEY_PRESS_MASK);
  g_signal_connect(G_OBJECT(mainWindow), "key_press_event",
                   G_CALLBACK(keyPressCallback), NULL);

  /*
   * Configure window properties
   */
  gtk_window_set_position(GTK_WINDOW(mainWindow), GTK_WIN_POS_CENTER);
  gtk_window_set_title(GTK_WINDOW(mainWindow), "vimpaint");
  gtk_widget_set_size_request(drawingArea, 2000, 2000);

  /*
   * Add the containers
   */
  gtk_container_add(GTK_CONTAINER(mainWindow), box);
  gtk_container_add(GTK_CONTAINER(box), drawingArea);

  gtk_widget_show_all(mainWindow);
  gtk_main();
}
