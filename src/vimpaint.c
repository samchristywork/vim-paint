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
double zoom = .1;

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
   * Draw modeline
   */
  char buf[256];
  snprintf(buf, 255, "This is where the modeline will be.");
  cairo_move_to(cr, 10, height - 10);
  cairo_set_font_size(cr, fontSize);
  cairo_set_source_rgba(cr, 1, 0, 0, 1);
  cairo_show_text(cr, buf);
  cairo_fill(cr);
}

int main(int argc, char *argv[]) {
  gtk_init(&argc, &argv);

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
  gtk_window_set_title(GTK_WINDOW(mainWindow), "Hello, World!");
  gtk_widget_set_size_request(drawingArea, 2000, 2000);

  /*
   * Add the containers
   */
  gtk_container_add(GTK_CONTAINER(mainWindow), box);
  gtk_container_add(GTK_CONTAINER(box), drawingArea);

  gtk_widget_show_all(mainWindow);
  gtk_main();
}
