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

struct color {
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char a;
};

struct color *palette[9];

struct layer {
  unsigned char *pixels;
  int width;
  int height;
};

struct layer layers[9];

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
 * Callback for keyboard events
 */
gboolean keyPressCallback(GtkWidget *widget, GdkEventKey *event,
                          gpointer data) {

  /*
   * Exit when the user presses Escape
   */
  if (event->keyval == GDK_KEY_Escape) {
    exit(EXIT_SUCCESS);
    return TRUE;
  }

  else if (event->keyval == GDK_KEY_minus) {
    zoom /= 1.1;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  } else if (event->keyval == GDK_KEY_plus) {
    zoom *= 1.1;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  }

  else if (event->keyval == GDK_KEY_H) {
    cursorPositionX -= shiftMultiplier;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  } else if (event->keyval == GDK_KEY_J) {
    cursorPositionY += shiftMultiplier;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  } else if (event->keyval == GDK_KEY_K) {
    cursorPositionY -= shiftMultiplier;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  } else if (event->keyval == GDK_KEY_L) {
    cursorPositionX += shiftMultiplier;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  }

  else if (event->keyval == GDK_KEY_h) {
    cursorPositionX--;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  } else if (event->keyval == GDK_KEY_j) {
    cursorPositionY++;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  } else if (event->keyval == GDK_KEY_k) {
    cursorPositionY--;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  } else if (event->keyval == GDK_KEY_l) {
    cursorPositionX++;
    gdk_window_invalidate_rect(gtk_widget_get_window(drawingArea), NULL, TRUE);
    return TRUE;
  }

  else {
    printf("%d\n", event->keyval);
  }
  return FALSE;
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
          cairo_save(cr);
          cairo_translate(cr, x * pixelSize * zoom, y * pixelSize * zoom);
          cairo_rectangle(cr, 0, 0, pixelSize * zoom, pixelSize * zoom);
          cairo_set_source_rgba(cr, r, g, b, 1);
          cairo_fill(cr);
          cairo_restore(cr);
        }
      }
    }
  }

  /*
   * Draw modeline
   */
  char buf[256];
  snprintf(buf, 255, "This is where the modeline will be.");
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
}

int main(int argc, char *argv[]) {
  gtk_init(&argc, &argv);

  for (int i = 0; i < 9; i++) {
    layers[i].width = 100;
    layers[i].height = 100;
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
  backgroundImage = cairo_image_surface_create_from_png("res/test.png");
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
