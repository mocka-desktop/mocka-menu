/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <string.h>

#include "app-item.h"
#include "app-menu.h"
#include "menu-window.h"

#define ROW_ICON_SIZE 24
#define COLUMN_ICON_SIZE 32
#define TILE_ICON_SIZE 64
#define TILE_WIDTH 160
/* The icon, two lines of name, and the padding around them. */
#define TILE_HEIGHT 136
#define ROW_SPACING 8
#define ROW_PADDING 4

struct _MockaAppDrag
{
  GObject parent_instance;

  MockaMenuWindow *window; /* weak, the window the running drag started in */
  GtkWidget *source;       /* what is being dragged, while it lasts */
  gulong source_destroy;
};

G_DEFINE_TYPE (MockaAppDrag, mocka_app_drag, G_TYPE_OBJECT)

enum
{
  SIGNAL_FINISHED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
mocka_app_drag_init (MockaAppDrag *self)
{
}

static void
mocka_app_drag_class_init (MockaAppDragClass *klass)
{
  signals[SIGNAL_FINISHED]
      = g_signal_new ("finished", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

MockaAppDrag *
mocka_app_drag_get (void)
{
  static MockaAppDrag *singleton;

  if (singleton == NULL)
    {
      singleton = g_object_new (MOCKA_TYPE_APP_DRAG, NULL);
    }

  return singleton;
}

gboolean
mocka_app_drag_is_running (void)
{
  return mocka_app_drag_get ()->window != NULL;
}

GIcon *
mocka_icon_to_draw (GIcon *icon, const gchar *generic)
{
  GtkIconTheme *theme = gtk_icon_theme_get_default ();
  const gchar *const *names;

  if (icon == NULL)
    {
      return g_themed_icon_new (generic);
    }

  if (!G_IS_THEMED_ICON (icon))
    {
      return g_object_ref (icon);
    }

  /*
   * GTK searches theme by theme and tries every name of an icon within each
   * theme, so merging a generic name into the icon would let the current
   * theme's generic beat an application's own icon in hicolor.
   */
  names = g_themed_icon_get_names (G_THEMED_ICON (icon));
  for (gsize i = 0; names != NULL && names[i] != NULL; i++)
    {
      if (gtk_icon_theme_has_icon (theme, names[i]))
        {
          return g_object_ref (icon);
        }
    }

  return g_themed_icon_new (generic);
}

enum
{
  TARGET_APP,
  TARGET_URI_LIST
};

/*
 * Dragged out, an app is the URI of its desktop entry, which is what the
 * desktop, a panel and Mocka Dock all make a launcher from (SPEC section 13).
 */
static const GtkTargetEntry drag_targets[] = {
  { (gchar *)MOCKA_APP_ITEM_TARGET, GTK_TARGET_SAME_APP, TARGET_APP },
  { (gchar *)"text/uri-list", 0, TARGET_URI_LIST },
};

/* A drop takes our own apps only, never files from elsewhere. */
static const GtkTargetEntry drop_targets[] = {
  { (gchar *)MOCKA_APP_ITEM_TARGET, GTK_TARGET_SAME_APP, TARGET_APP },
};

// NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange) two actions combined is not one named value
static const GdkDragAction APP_DRAG_ACTIONS = GDK_ACTION_COPY | GDK_ACTION_MOVE;
// NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)

const GtkTargetEntry *
mocka_app_item_drop_targets (guint *n_targets)
{
  *n_targets = G_N_ELEMENTS (drop_targets);
  return drop_targets;
}

GdkDragAction
mocka_app_item_drag_actions (void)
{
  return APP_DRAG_ACTIONS;
}

gchar *
mocka_app_item_id_from_drop (GtkSelectionData *selection)
{
  const guchar *raw = gtk_selection_data_get_data (selection);
  gint length = gtk_selection_data_get_length (selection);

  if (raw == NULL || length <= 0)
    {
      return NULL;
    }

  return g_strndup ((const gchar *)raw, (gsize)length);
}

MockaMenuApp *
mocka_app_item_get_app (GtkWidget *widget)
{
  return widget != NULL ? g_object_get_data (G_OBJECT (widget), "app") : NULL;
}

GtkRequisition
mocka_app_item_tile_size (void)
{
  GtkRequisition size = { TILE_WIDTH, TILE_HEIGHT };

  return size;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) the drag-data-get signal fixes this signature
static void
on_drag_data_get (GtkWidget *events, GdkDragContext *context, GtkSelectionData *selection, guint info, guint time,
                  gpointer user_data)
{
  MockaMenuApp *app = mocka_app_item_get_app (events);
  const gchar *file;
  gchar *uris[2] = { NULL, NULL };

  if (app == NULL)
    {
      return;
    }

  if (info == TARGET_APP)
    {
      gtk_selection_data_set (selection, gdk_atom_intern_static_string (MOCKA_APP_ITEM_TARGET), 8,
                              (const guchar *)mocka_menu_app_get_id (app), (gint)strlen (mocka_menu_app_get_id (app)));
      return;
    }

  file = g_desktop_app_info_get_filename (mocka_menu_app_get_app_info (app));
  if (file == NULL)
    {
      return;
    }

  uris[0] = g_filename_to_uri (file, NULL, NULL);
  if (uris[0] != NULL)
    {
      gtk_selection_data_set_uris (selection, uris);
    }

  g_free (uris[0]);
}
// NOLINTEND(bugprone-easily-swappable-parameters)

static void on_source_destroyed (GtkWidget *source, gpointer user_data);

static void
finish_drag (MockaAppDrag *drag)
{
  MockaMenuWindow *window = drag->window;

  if (drag->source != NULL)
    {
      g_signal_handler_disconnect (drag->source, drag->source_destroy);
      drag->source = NULL;
      drag->source_destroy = 0;
    }

  if (window == NULL)
    {
      return;
    }

  g_object_remove_weak_pointer (G_OBJECT (window), (gpointer *)&drag->window);
  drag->window = NULL;
  mocka_menu_window_end_grab_handover (window);

  g_signal_emit (drag, signals[SIGNAL_FINISHED], 0);
}

/*
 * A rebuild can destroy what is being dragged, and GTK emits no drag-end for
 * a destroyed source. Without this the drag would look like it never ended:
 * the window would never take its grab back and every later drag would be
 * refused.
 */
static void
on_source_destroyed (GtkWidget *source, gpointer user_data)
{
  finish_drag (mocka_app_drag_get ());
}

static void
on_drag_begin (GtkWidget *events, GdkDragContext *context, gpointer user_data)
{
  MockaAppDrag *drag = mocka_app_drag_get ();
  MockaMenuApp *app = mocka_app_item_get_app (events);
  GIcon *icon = app != NULL ? mocka_menu_app_get_icon (app) : NULL;
  MockaMenuWindow *window = mocka_menu_window_of (events);

  if (icon != NULL)
    {
      gtk_drag_set_icon_gicon (context, icon, 0, 0);
    }

  if (window == NULL || drag->window != NULL)
    {
      return;
    }

  /* The row can be destroyed before the drag ends, and then it can no longer
   * name its own window, so the window is remembered here. */
  drag->window = window;
  g_object_add_weak_pointer (G_OBJECT (window), (gpointer *)&drag->window);
  drag->source = events;
  drag->source_destroy = g_signal_connect (events, "destroy", G_CALLBACK (on_source_destroyed), NULL);
  mocka_menu_window_begin_grab_handover (window);
}

static void
on_drag_end (GtkWidget *events, GdkDragContext *context, gpointer user_data)
{
  finish_drag (mocka_app_drag_get ());
}

static gboolean
on_button_press (GtkWidget *events, GdkEventButton *event, MockaFavourites *favourites)
{
  MockaMenuApp *app = mocka_app_item_get_app (events);

  if (event->button != GDK_BUTTON_SECONDARY || app == NULL || favourites == NULL)
    {
      return GDK_EVENT_PROPAGATE;
    }

  mocka_app_menu_popup (app, favourites, events, (const GdkEvent *)event);

  return GDK_EVENT_STOP;
}

/* GTK emits this for the Menu key and Shift + F10 (SPEC section 12.2). */
static gboolean
on_popup_menu (GtkWidget *item, MockaFavourites *favourites)
{
  MockaMenuApp *app = mocka_app_item_get_app (item);

  if (app == NULL || favourites == NULL)
    {
      return FALSE;
    }

  mocka_app_menu_popup (app, favourites, item, NULL);

  return TRUE;
}

/*
 * A list row has no window of its own, so a drag source on it never sees the
 * button events. The content goes inside an event box, which has one, and
 * that is what drags and what catches the right click.
 */
static GtkWidget *
draggable_holder (GtkWidget *item, MockaMenuApp *app, MockaFavourites *favourites)
{
  GtkWidget *events = gtk_event_box_new ();

  gtk_event_box_set_visible_window (GTK_EVENT_BOX (events), FALSE);
  gtk_container_add (GTK_CONTAINER (item), events);

  /* The item holds the reference; this one only borrows it. */
  g_object_set_data (G_OBJECT (events), "app", app);

  gtk_drag_source_set (events, GDK_BUTTON1_MASK, drag_targets, G_N_ELEMENTS (drag_targets), APP_DRAG_ACTIONS);
  g_signal_connect (events, "drag-data-get", G_CALLBACK (on_drag_data_get), NULL);
  g_signal_connect (events, "drag-begin", G_CALLBACK (on_drag_begin), NULL);
  g_signal_connect (events, "drag-end", G_CALLBACK (on_drag_end), NULL);
  g_signal_connect (events, "button-press-event", G_CALLBACK (on_button_press), favourites);
  g_signal_connect (item, "popup-menu", G_CALLBACK (on_popup_menu), favourites);

  return events;
}

static GtkWidget *
app_image (MockaMenuApp *app, gint size)
{
  GIcon *icon = mocka_icon_to_draw (mocka_menu_app_get_icon (app), "application-x-executable");
  GtkWidget *image = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_LARGE_TOOLBAR);

  g_object_unref (icon);
  gtk_image_set_pixel_size (GTK_IMAGE (image), size);

  return image;
}

GtkWidget *
mocka_app_item_new_row (MockaMenuApp *app, MockaFavourites *favourites)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, ROW_SPACING);
  GtkWidget *label = gtk_label_new (mocka_menu_app_get_name (app));
  const gchar *comment = mocka_menu_app_get_comment (app);

  gtk_label_set_xalign (GTK_LABEL (label), 0.0F);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);

  gtk_box_pack_start (GTK_BOX (box), app_image (app, ROW_ICON_SIZE), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), label, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (box), ROW_PADDING);

  if (comment != NULL && *comment != '\0')
    {
      gtk_widget_set_tooltip_text (row, comment);
    }

  g_object_set_data_full (G_OBJECT (row), "app", g_object_ref (app), g_object_unref);
  gtk_container_add (GTK_CONTAINER (draggable_holder (row, app, favourites)), box);

  return row;
}

GtkWidget *
mocka_app_item_new_icon (MockaMenuApp *app, MockaFavourites *favourites, gint pixel_size)
{
  GtkWidget *row = gtk_list_box_row_new ();
  GtkWidget *image = app_image (app, pixel_size > 0 ? pixel_size : COLUMN_ICON_SIZE);

  gtk_widget_set_margin_top (image, ROW_PADDING);
  gtk_widget_set_margin_bottom (image, ROW_PADDING);
  gtk_widget_set_margin_start (image, ROW_PADDING);
  gtk_widget_set_margin_end (image, ROW_PADDING);

  gtk_widget_set_tooltip_text (row, mocka_menu_app_get_name (app));
  g_object_set_data_full (G_OBJECT (row), "app", g_object_ref (app), g_object_unref);
  gtk_container_add (GTK_CONTAINER (draggable_holder (row, app, favourites)), image);

  return row;
}

GtkWidget *
mocka_app_item_new_tile (MockaMenuApp *app, MockaFavourites *favourites)
{
  GtkWidget *tile = gtk_flow_box_child_new ();
  GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, ROW_PADDING);
  GtkWidget *label = gtk_label_new (mocka_menu_app_get_name (app));
  const gchar *comment = mocka_menu_app_get_comment (app);

  /* Two lines, then an ellipsis (SPEC section 7). */
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_label_set_line_wrap_mode (GTK_LABEL (label), PANGO_WRAP_WORD_CHAR);
  gtk_label_set_lines (GTK_LABEL (label), 2);
  gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_CENTER);
  gtk_label_set_max_width_chars (GTK_LABEL (label), 16);

  gtk_box_pack_start (GTK_BOX (box), app_image (app, TILE_ICON_SIZE), FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (box), ROW_SPACING);
  gtk_widget_set_size_request (tile, TILE_WIDTH, TILE_HEIGHT);

  if (comment != NULL && *comment != '\0')
    {
      gtk_widget_set_tooltip_text (tile, comment);
    }

  g_object_set_data_full (G_OBJECT (tile), "app", g_object_ref (app), g_object_unref);
  gtk_container_add (GTK_CONTAINER (draggable_holder (tile, app, favourites)), box);

  return tile;
}
