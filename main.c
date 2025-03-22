

#define APPLICATION_ID "com.github.xpncvr.simple-gtk-drawer"
// inspired by https://toshiocp.github.io/Gtk4-tutorial/sec26.html

#include <cairo-svg.h>
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>

static cairo_surface_t *surface = NULL;
static cairo_surface_t *surface_save = NULL;
static double start_x;
static double start_y;
static uint8_t draw_mode = 1;

static gboolean is_grid = FALSE;

static gboolean is_typing = FALSE;
static gboolean is_shift = FALSE;

static GString *global_buffer = NULL;
static int typing_x = 0;
static int typing_y = 0;

static GList *actions = NULL;

static gboolean help_menu = FALSE;

typedef enum {
  SHAPE_LINE,
  SHAPE_SQUARE,
  SHAPE_CIRCLE,
  SHAPE_TEXT,
} ShapeType;

typedef struct {
  ShapeType type;
  int x1, y1;
  int x2, y2;
  GString *text;
} DrawingAction;

static DrawingAction *last_action = NULL;

typedef struct {
  GtkWidget *entry;
  GtkWidget *widget;
} CallbackData;

static void add_drawing_action(ShapeType type, int x1, int y1, int x2, int y2) {
  DrawingAction *action = g_malloc(sizeof(DrawingAction));
  action->type = type;
  action->x1 = x1;
  action->y1 = y1;
  action->x2 = x2;
  action->y2 = y2;

  actions = g_list_append(actions, action);
}

static void add_text_action(GString *text, int x1, int y1) {
  DrawingAction *action = g_malloc(sizeof(DrawingAction));
  action->type = SHAPE_TEXT;
  action->x1 = x1;
  action->y1 = y1;
  action->x2 = 0;
  action->y2 = 0;
  action->text = g_string_new(text->str);
  actions = g_list_append(actions, action);
}

void remove_last_drawing_action() {
  if (actions != NULL) {
    GList *last_element = g_list_last(actions);

    if (last_element->data != NULL) {
      if (last_action) {
        g_free(last_action);
      }
      last_action = g_malloc(sizeof(DrawingAction));
      *last_action = *(DrawingAction *)last_element->data;
    }
    g_free(last_element->data);
    actions = g_list_delete_link(actions, last_element);
  }
}

void redo_last_action() {
  if (actions != NULL && last_action != NULL) {
    DrawingAction *new_action = malloc(sizeof(DrawingAction));
    if (new_action != NULL) {
      *new_action = *last_action;

      actions = g_list_append(actions, new_action);
      g_free(last_action);
      last_action = NULL;
    }
  }
}

static void copy_surface(cairo_surface_t *src, cairo_surface_t *dst) {
  if (!src || !dst)
    return;
  cairo_t *cr = cairo_create(dst);
  cairo_set_source_surface(cr, src, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
}

static void clear_drawing_area() {
  if (surface) {
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_paint(cr);
    cairo_destroy(cr);
  }

  g_list_free_full(actions, g_free);
  actions = NULL;
}

static void redraw_surface(gpointer user_data);

static void resize_cb(GtkWidget *widget, int width, int height,
                      gpointer user_data) {
  cairo_t *cr;

  if (surface)
    cairo_surface_destroy(surface);
  surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);
  if (surface_save)
    cairo_surface_destroy(surface_save);
  surface_save = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);

  cr = cairo_create(surface);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  redraw_surface(user_data);
}

static void draw_cb(GtkDrawingArea *da, cairo_t *cr, int width, int height,
                    gpointer user_data) {
  if (surface) {
    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_paint(cr);
  }

  if (help_menu) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 20);
    cairo_set_source_rgb(cr, 0, 0, 0);

    double x = 20, y = 40;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, "Help Menu:");

    cairo_move_to(cr, x, y + 30);
    cairo_show_text(cr, "1 - Draw Line");

    cairo_move_to(cr, x, y + 60);
    cairo_show_text(cr, "2 - Draw Rectangle, hold shift to draw square");

    cairo_move_to(cr, x, y + 90);
    cairo_show_text(cr, "3 - Draw Circle");

    cairo_move_to(cr, x, y + 120);
    cairo_show_text(cr, "4 - Write text");

    cairo_move_to(cr, x, y + 150);
    cairo_show_text(cr, "x - Clear Drawing Area");

    cairo_move_to(cr, x, y + 180);
    cairo_show_text(cr, "Ctrl+Z - Undo");

    cairo_move_to(cr, x, y + 210);
    cairo_show_text(cr, "Ctrl+Shift+Z - Redo last action");

    cairo_move_to(cr, x, y + 240);
    cairo_show_text(cr, "g - Toggle grid snapping");

    cairo_move_to(cr, x, y + 270);
    cairo_show_text(cr, "Control + S - Save");

    cairo_move_to(cr, x, y + 300);
    cairo_show_text(cr, "h - Toggle Help Menu");

    cairo_stroke(cr);
  }
}

static void drag_begin(GtkGestureDrag *gesture, double x, double y,
                       gpointer user_data) {
  copy_surface(surface, surface_save);
  if (is_grid) {
    start_x = round(x / 10.0) * 10;
    start_y = round(y / 10.0) * 10;
  } else {
    start_x = x;
    start_y = y;
  }
}

static void drag_update(GtkGestureDrag *gesture, double offset_x,
                        double offset_y, gpointer user_data) {
  GtkWidget *da = GTK_WIDGET(user_data);
  cairo_t *cr;

  copy_surface(surface_save, surface);
  cr = cairo_create(surface);
  if (is_grid) {
    offset_x = round(offset_x / 10.0) * 10;
    offset_y = round(offset_y / 10.0) * 10;
  }
  int end_x = start_x + offset_x;
  int end_y = start_y + offset_y;

  if (draw_mode == 1) {
    cairo_move_to(cr, start_x, start_y);
    cairo_line_to(cr, end_x, end_y);
  } else if (draw_mode == 2) {
    if (is_shift) {
      cairo_rectangle(cr, start_x, start_y, offset_x,
                      (offset_x > 0 ? (offset_y < 0 ? -offset_x : offset_x)
                                    : (offset_y < 0 ? offset_x : -offset_x)));
    } else {
      cairo_rectangle(cr, start_x, start_y, offset_x, offset_y);
    }
  } else if (draw_mode == 3) {
    double radius = sqrt(offset_x * offset_x + offset_y * offset_y);
    cairo_arc(cr, start_x, start_y, radius, 0, 2 * G_PI);
  } else if (draw_mode == 4) {
  }
  cairo_stroke(cr);
  cairo_destroy(cr);
  gtk_widget_queue_draw(da);
}

static void draw_action(cairo_t *cr, DrawingAction *action) {
  switch (action->type) {
  case SHAPE_LINE:
    cairo_move_to(cr, action->x1, action->y1);
    cairo_line_to(cr, action->x2, action->y2);
    break;
  case SHAPE_SQUARE:
    cairo_rectangle(cr, action->x1, action->y1, action->x2 - action->x1,
                    action->y2 - action->y1);
    break;
  case SHAPE_CIRCLE:
    double radius = sqrt((action->x2 - action->x1) * (action->x2 - action->x1) +
                         (action->y2 - action->y1) * (action->y2 - action->y1));
    cairo_new_sub_path(cr);
    cairo_arc(cr, action->x1, action->y1, radius, 0, 2 * G_PI);
    break;
  case SHAPE_TEXT:
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20);
    cairo_move_to(cr, action->x1, action->y1 + 10);
    cairo_show_text(cr, action->text->str);
    break;
  default:
    break;
  }
}

static void redraw_surface(gpointer user_data) {
  GtkWidget *da = GTK_WIDGET(user_data);
  cairo_t *cr;

  cairo_t *clear_cr = cairo_create(surface);
  cairo_set_source_rgb(clear_cr, 1.0, 1.0, 1.0);
  cairo_paint(clear_cr);
  cairo_destroy(clear_cr);

  cr = cairo_create(surface);

  for (GList *iter = actions; iter != NULL; iter = iter->next) {
    DrawingAction *action = (DrawingAction *)iter->data;
    draw_action(cr, action);
  }

  cairo_stroke(cr);
  cairo_destroy(cr);
  if (GTK_IS_WIDGET(da)) {
    gtk_widget_queue_draw(da);
  }
}

static void drag_end(GtkGestureDrag *gesture, double offset_x, double offset_y,
                     gpointer user_data) {
  if (is_grid) {
    offset_x = round(offset_x / 10.0) * 10;
    offset_y = round(offset_y / 10.0) * 10;
  }
  int end_x = start_x + offset_x;
  int end_y = start_y + offset_y;

  if (draw_mode == 1) {
    add_drawing_action(SHAPE_LINE, start_x, start_y, end_x, end_y);
  } else if (draw_mode == 2) {
    if (is_shift) {
      add_drawing_action(
          SHAPE_SQUARE, start_x, start_y, end_x,
          start_y + (offset_x > 0 ? (offset_y < 0 ? -offset_x : offset_x)
                                  : (offset_y < 0 ? offset_x : -offset_x)));
    } else {
      add_drawing_action(SHAPE_SQUARE, start_x, start_y, end_x, end_y);
    }

  } else if (draw_mode == 3) {

    add_drawing_action(SHAPE_CIRCLE, start_x, start_y, end_x, end_y);
  }
  redraw_surface(user_data);
}

static void export_to_png(GtkWidget *drawing_area, const char *filename) {
  GtkAllocation allocation;
  gtk_widget_get_allocation(drawing_area, &allocation);
  int surface_width = allocation.width;
  int surface_height = allocation.height;
  cairo_surface_t *image_surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, surface_width, surface_height);
  cairo_t *cr = cairo_create(image_surface);

  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_paint(cr);

  cairo_status_t status = cairo_surface_write_to_png(image_surface,filename);
  if (status == CAIRO_STATUS_SUCCESS) {
      g_message("Successfully wrote to PNG file: %s", filename);
  } else {
      g_message("Failed to write to PNG file: %s. Error: %s", filename, cairo_status_to_string(status));
  }

  cairo_destroy(cr);
  cairo_surface_destroy(image_surface);
}

static void export_to_svg(GtkWidget *drawing_area, const char *filename) {
  GtkAllocation allocation;
  gtk_widget_get_allocation(drawing_area, &allocation);
  int surface_width = allocation.width;
  int surface_height = allocation.height;
  cairo_surface_t *svg_surface =
      cairo_svg_surface_create(filename, surface_width, surface_height);
  cairo_t *cr = cairo_create(svg_surface);

  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_paint(cr);

  cairo_destroy(cr);
  cairo_surface_destroy(svg_surface);
}
static void on_save_response(GtkDialog *dialog, int response, gpointer user_data) {

  if (response == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);

    g_autoptr(GFile) file = gtk_file_chooser_get_file(chooser);
    gchar *file_path = g_file_get_path(file);
    export_to_png(GTK_WIDGET(user_data), file_path);
    g_message("Exporting... %s", file_path);

  }

  gtk_window_destroy(GTK_WINDOW(dialog));
}

static void save_popup(GtkWidget *widget, GtkWidget *da) {
  GtkWindow *parent = GTK_WINDOW(gtk_widget_get_root(widget));

  GtkWidget *dialog;
  GtkFileChooser *chooser;
  GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SAVE;

  dialog = gtk_file_chooser_dialog_new("Save File", parent, action,
                                       ("_Cancel"), GTK_RESPONSE_CANCEL,
                                       ("_Save"), GTK_RESPONSE_ACCEPT, NULL);
  chooser = GTK_FILE_CHOOSER(dialog);

  gtk_file_chooser_set_current_name(chooser, ("image.png"));

  gtk_window_present(GTK_WINDOW(dialog));

  g_signal_connect(dialog, "response", G_CALLBACK(on_save_response), da);
}

static gboolean key_press_cb(GtkEventControllerKey *controller, guint keyval,
                             guint keycode, GdkModifierType state,
                             gpointer user_data) {

  if (is_typing) {
    GtkWidget *da = GTK_WIDGET(user_data);
    cairo_t *cr;

    if (keyval == GDK_KEY_Return) {
      is_typing = FALSE;
      add_text_action(global_buffer, typing_x, typing_y);
      g_string_set_size(global_buffer, 0);
      redraw_surface(user_data);
    } else {
      if (keyval == GDK_KEY_BackSpace) {
        if (global_buffer->len > 0) {
          g_string_set_size(global_buffer, global_buffer->len - 1);
        }
      } else {
        gunichar key_char = gdk_keyval_to_unicode(keyval);
        if (key_char) {
          g_string_append_unichar(global_buffer, key_char);
        }
      }
      copy_surface(surface_save, surface);
    }

    cr = cairo_create(surface);

    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 20);
    cairo_move_to(cr, typing_x, typing_y + 10);
    cairo_show_text(cr, global_buffer->str);

    cairo_stroke(cr);
    cairo_destroy(cr);
    gtk_widget_queue_draw(da);

    return GDK_EVENT_PROPAGATE;
  }

  if (keyval == GDK_KEY_1) {
    draw_mode = 1;
  } else if (keyval == GDK_KEY_2) {
    draw_mode = 2;
  } else if (keyval == GDK_KEY_3) {
    draw_mode = 3;
  } else if (keyval == GDK_KEY_4) {
    draw_mode = 4;
  } else if (keyval == GDK_KEY_5) {
    draw_mode = 5;
  } else if (keyval == GDK_KEY_x) {
    GtkWidget *da = GTK_WIDGET(user_data);
    clear_drawing_area();
    gtk_widget_queue_draw(da);
  } else if (keyval == GDK_KEY_h) {
    help_menu = !help_menu;
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
  } else if (keyval == GDK_KEY_g) {
    is_grid = !is_grid;
  } else if (keyval == GDK_KEY_Shift_L) {
    is_shift = TRUE;
  } else if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_z) {
    remove_last_drawing_action();
    redraw_surface(user_data);
  } else if ((state & GDK_CONTROL_MASK) &&
             (keyval == GDK_KEY_y ||
              (state & GDK_SHIFT_MASK && keyval == GDK_KEY_Z))) {
    redo_last_action();
    redraw_surface(user_data);
  } else if ((state & GDK_CONTROL_MASK) && keyval == GDK_KEY_s) {
    save_popup(GTK_WIDGET(user_data), GTK_WIDGET(user_data));
  }
  return GDK_EVENT_PROPAGATE;
}

static gboolean key_release_cb(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state,
                               gpointer user_data) {

  if (keyval == GDK_KEY_Shift_L) {
    is_shift = FALSE;
  }
  return GDK_EVENT_PROPAGATE;
}

static void left_click_cb(GtkGestureClick *gesture, int n_press, double x,
                          double y, gpointer user_data) {
  if (n_press == 1) {
    if (is_typing) {
      is_typing = FALSE;
      add_text_action(global_buffer, typing_x, typing_y);
      g_string_set_size(global_buffer, 0);
      redraw_surface(user_data);
      return;
    }
    if (draw_mode == 4) {
      is_typing = TRUE;
      typing_x = x;
      typing_y = y;
      copy_surface(surface, surface_save);
    }
  }
}

static void app_activate(GApplication *application) {
  GtkApplication *app = GTK_APPLICATION(application);
  GtkWindow *win;

  win = gtk_application_get_active_window(app);
  gtk_window_present(win);
}

static void app_shutdown(GApplication *application) {
  if (surface)
    cairo_surface_destroy(surface);
  if (surface_save)
    cairo_surface_destroy(surface_save);
}

static void app_startup(GApplication *application) {
  GtkApplication *app = GTK_APPLICATION(application);
  GtkBuilder *build;
  GtkWindow *win;
  GtkDrawingArea *da;
  GtkGesture *drag;
  GtkGesture *click;
  GtkEventController *key_controller;

  build = gtk_builder_new_from_file("./main.ui");
  win = GTK_WINDOW(gtk_builder_get_object(build, "win"));
  da = GTK_DRAWING_AREA(gtk_builder_get_object(build, "da"));
  gtk_window_set_resizable(win, TRUE);
  gtk_window_set_application(win, app);
  g_object_unref(build);

  gtk_drawing_area_set_draw_func(da, draw_cb, NULL, NULL);
  g_signal_connect_after(da, "resize", G_CALLBACK(resize_cb), NULL);

  drag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller(GTK_WIDGET(da), GTK_EVENT_CONTROLLER(drag));
  g_signal_connect(drag, "drag-begin", G_CALLBACK(drag_begin), NULL);
  g_signal_connect(drag, "drag-update", G_CALLBACK(drag_update), da);
  g_signal_connect(drag, "drag-end", G_CALLBACK(drag_end), da);

  click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller(GTK_WIDGET(da), GTK_EVENT_CONTROLLER(click));
  g_signal_connect(click, "pressed", G_CALLBACK(left_click_cb), da);

  key_controller = gtk_event_controller_key_new();
  gtk_widget_add_controller(GTK_WIDGET(win), key_controller);
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(key_press_cb), da);
  g_signal_connect(key_controller, "key-released", G_CALLBACK(key_release_cb),
                   da);

  global_buffer = g_string_new("");
}

int main(int argc, char **argv) {
  if (argc > 1) {
    printf("This is a gui application. Press h in the window to display "
           "controls.\n");
    return 0;
  }
  GtkApplication *app;
  int stat;

  app = gtk_application_new(APPLICATION_ID, G_APPLICATION_HANDLES_OPEN);
  g_signal_connect(app, "startup", G_CALLBACK(app_startup), NULL);
  g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
  g_signal_connect(app, "shutdown", G_CALLBACK(app_shutdown), NULL);
  stat = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return stat;
}
