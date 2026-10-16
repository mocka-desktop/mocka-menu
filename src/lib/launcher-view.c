/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/*
 * The Launcher layout (SPEC section 7). The window covers the monitor, so
 * this paints its own background and closes on a press that reaches it,
 * meaning a press on empty space.
 *
 * The grid is paged rather than scrolled: how many tiles fit is worked out
 * from the room the grid is given, and the apps are cut into pages of that
 * size, each page a flow box of its own inside a stack.
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "app-item.h"
#include "category-list.h"
#include "launcher-view.h"
#include "menu-window.h"
#include "search.h"
#include "session.h"

/* How much of the desktop shows through, with a compositor. */
#define BACKGROUND_ALPHA 0.92

#define EDGE_PADDING 24
#define SEARCH_WIDTH 36

/* The block's size in tiles, when the window has room for all of them. */
#define GRID_COLUMNS 7
#define GRID_ROWS 4
#define GRID_SPACING 12

#define DOT_SIZE 10

/* Beside 64px tiles, the Classic size looks undersized. */
#define COLUMN_ICON_SIZE 48

struct _MockaLauncherView
{
  GtkEventBox parent_instance;

  MockaMenuData *data;         /* not owned */
  MockaFavourites *favourites; /* not owned */

  GtkWidget *entry;
  GtkWidget *column; /* favourites above the session controls */
  GtkWidget *categories;
  GtkWidget *pages; /* a flow box per page */
  GtkWidget *previous;
  GtkWidget *next;
  GtkWidget *dots;

  GPtrArray *apps; /* MockaMenuApp*, what the pages are cut from */
  guint columns;
  guint rows;
  guint page_count;
  guint page;

  gboolean search_pending;
  gulong toplevel_key;
};

G_DEFINE_TYPE (MockaLauncherView, mocka_launcher_view, GTK_TYPE_EVENT_BOX)

enum
{
  SIGNAL_APP_ACTIVATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void on_search_changed (GtkSearchEntry *entry, MockaLauncherView *self);
static void set_page (MockaLauncherView *self, guint page);

static void
emit_activated (MockaLauncherView *self, MockaMenuApp *app)
{
  if (app != NULL)
    {
      g_signal_emit (self, signals[SIGNAL_APP_ACTIVATED], 0, app);
    }
}

static void
destroy_child (GtkWidget *widget, gpointer user_data)
{
  gtk_widget_destroy (widget);
}

static void
on_child_activated (GtkFlowBox *page, GtkFlowBoxChild *child, MockaLauncherView *self)
{
  emit_activated (self, mocka_app_item_get_app (GTK_WIDGET (child)));
}

static GtkWidget *
current_page (MockaLauncherView *self)
{
  return gtk_stack_get_visible_child (GTK_STACK (self->pages));
}

static void
update_dots (MockaLauncherView *self)
{
  gtk_widget_queue_draw (self->dots);
}

/*
 * Drawn rather than themed: a button's checked state looks the same as its
 * normal one in most themes, which would leave every page looking current.
 */
static gboolean
on_dot_draw (GtkWidget *dot, cairo_t *cr, MockaLauncherView *self)
{
  GtkStyleContext *context = gtk_widget_get_style_context (dot);
  guint page = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (dot), "page"));
  GtkAllocation alloc;
  GdkRGBA colour;

  gtk_widget_get_allocation (dot, &alloc);
  gtk_style_context_get_color (context, gtk_style_context_get_state (context), &colour);

  colour.alpha = page == self->page ? 1.0 : 0.35;
  gdk_cairo_set_source_rgba (cr, &colour);

  cairo_arc (cr, alloc.width / 2.0, alloc.height / 2.0, DOT_SIZE / 2.0, 0, 2 * G_PI);
  cairo_fill (cr);

  return GDK_EVENT_STOP;
}

/*
 * An arrow that would do nothing goes invisible rather than away: taking it
 * out of the layout would change the width of the grid area, and the centred
 * cluster around it would shift sideways on every page change.
 */
static void
set_arrow_usable (GtkWidget *arrow, gboolean usable)
{
  gtk_widget_set_opacity (arrow, usable ? 1.0 : 0.0);
  gtk_widget_set_sensitive (arrow, usable);
}

static void
update_arrows (MockaLauncherView *self)
{
  set_arrow_usable (self->previous, self->page_count > 1 && self->page > 0);
  set_arrow_usable (self->next, self->page_count > 1 && self->page + 1 < self->page_count);
  gtk_widget_set_visible (self->dots, self->page_count > 1);
}

static gboolean
on_dot_press (GtkWidget *dot, GdkEventButton *event, MockaLauncherView *self)
{
  if (event->button != GDK_BUTTON_PRIMARY)
    {
      return GDK_EVENT_PROPAGATE;
    }

  set_page (self, GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (dot), "page")));

  return GDK_EVENT_STOP;
}

static void
build_dots (MockaLauncherView *self)
{
  gtk_container_foreach (GTK_CONTAINER (self->dots), destroy_child, NULL);

  for (guint i = 0; i < self->page_count; i++)
    {
      GtkWidget *dot = gtk_event_box_new ();

      gtk_event_box_set_visible_window (GTK_EVENT_BOX (dot), FALSE);
      gtk_widget_set_size_request (dot, DOT_SIZE * 2, DOT_SIZE * 2);
      gtk_widget_set_valign (dot, GTK_ALIGN_CENTER);
      g_object_set_data (G_OBJECT (dot), "page", GUINT_TO_POINTER (i));
      g_signal_connect (dot, "draw", G_CALLBACK (on_dot_draw), self);
      g_signal_connect (dot, "button-press-event", G_CALLBACK (on_dot_press), self);

      gtk_box_pack_start (GTK_BOX (self->dots), dot, FALSE, FALSE, 0);
      /* One by one: show_all does nothing on the box, which is no_show_all so
       * that a single page keeps it hidden. */
      gtk_widget_show (dot);
    }

  update_dots (self);
}

static void
set_page (MockaLauncherView *self, guint page)
{
  gchar *name;

  if (self->page_count == 0)
    {
      return;
    }

  page = MIN (page, self->page_count - 1);
  name = g_strdup_printf ("%u", page);

  self->page = page;
  gtk_stack_set_visible_child_name (GTK_STACK (self->pages), name);
  update_dots (self);
  update_arrows (self);

  g_free (name);
}

static guint
tiles_per_page (MockaLauncherView *self)
{
  return MAX (1, self->columns * self->rows);
}

/* One flow box per page, each holding the tiles of that slice. */
static void
rebuild_pages (MockaLauncherView *self)
{
  guint per_page = tiles_per_page (self);
  guint total = self->apps != NULL ? self->apps->len : 0;
  guint wanted = self->page;

  gtk_container_foreach (GTK_CONTAINER (self->pages), destroy_child, NULL);

  self->page_count = total > 0 ? (total + per_page - 1) / per_page : 1;

  for (guint p = 0; p < self->page_count; p++)
    {
      GtkWidget *page = gtk_flow_box_new ();
      gchar *name = g_strdup_printf ("%u", p);

      gtk_flow_box_set_selection_mode (GTK_FLOW_BOX (page), GTK_SELECTION_BROWSE);
      gtk_flow_box_set_homogeneous (GTK_FLOW_BOX (page), TRUE);
      gtk_flow_box_set_min_children_per_line (GTK_FLOW_BOX (page), self->columns);
      gtk_flow_box_set_max_children_per_line (GTK_FLOW_BOX (page), self->columns);
      gtk_flow_box_set_row_spacing (GTK_FLOW_BOX (page), GRID_SPACING);
      gtk_flow_box_set_column_spacing (GTK_FLOW_BOX (page), GRID_SPACING);
      gtk_widget_set_halign (page, GTK_ALIGN_CENTER);
      gtk_widget_set_valign (page, GTK_ALIGN_CENTER);
      g_signal_connect (page, "child-activated", G_CALLBACK (on_child_activated), self);

      for (guint i = p * per_page; i < MIN ((p + 1) * per_page, total); i++)
        {
          gtk_container_add (GTK_CONTAINER (page),
                             mocka_app_item_new_tile (g_ptr_array_index (self->apps, i), self->favourites));
        }

      if (total == 0)
        {
          GtkWidget *empty = gtk_label_new (_ ("No results"));

          gtk_widget_set_sensitive (empty, FALSE);
          gtk_container_add (GTK_CONTAINER (page), empty);
        }

      gtk_stack_add_named (GTK_STACK (self->pages), page, name);
      g_free (name);
    }

  gtk_widget_show_all (self->pages);
  build_dots (self);
  set_page (self, MIN (wanted, self->page_count - 1));
}

/*
 * How many tiles the room allows. Measured from the window rather than from
 * the pages, whose size this then sets: asking the pages how much room they
 * have, and answering by resizing them, would never settle.
 */
static void
measure_grid (MockaLauncherView *self)
{
  GtkRequisition tile = mocka_app_item_tile_size ();
  GtkAllocation view;
  gint width;
  gint height;
  guint columns;
  guint rows;

  gtk_widget_get_allocation (GTK_WIDGET (self), &view);
  if (view.width <= 1 || view.height <= 1)
    {
      return;
    }

  width = view.width - (3 * EDGE_PADDING) - gtk_widget_get_allocated_width (self->column)
          - (2 * (gtk_widget_get_allocated_width (self->previous) + GRID_SPACING));
  height = view.height - (4 * EDGE_PADDING) - gtk_widget_get_allocated_height (self->entry)
           - gtk_widget_get_allocated_height (self->categories) - gtk_widget_get_allocated_height (self->dots)
           - GRID_SPACING;

  columns = (guint)CLAMP ((width + GRID_SPACING) / (tile.width + GRID_SPACING), 1, GRID_COLUMNS);
  rows = (guint)CLAMP ((height + GRID_SPACING) / (tile.height + GRID_SPACING), 1, GRID_ROWS);

  if (columns == self->columns && rows == self->rows)
    {
      return;
    }

  self->columns = columns;
  self->rows = rows;

  /* The block's own size, so the cluster around it closes up. */
  gtk_widget_set_size_request (self->pages, (gint)columns * (tile.width + GRID_SPACING) - GRID_SPACING,
                               (gint)rows * (tile.height + GRID_SPACING) - GRID_SPACING);
  rebuild_pages (self);
}

static void
on_size_allocate (GtkWidget *widget, GdkRectangle *allocation, MockaLauncherView *self)
{
  measure_grid (self);
}

static void
show_apps (MockaLauncherView *self, GPtrArray *apps)
{
  g_clear_pointer (&self->apps, g_ptr_array_unref);
  self->apps = g_ptr_array_new_with_free_func (g_object_unref);

  for (guint i = 0; apps != NULL && i < apps->len; i++)
    {
      g_ptr_array_add (self->apps, g_object_ref (g_ptr_array_index (apps, i)));
    }

  /* A new category starts on its first page. */
  self->page = 0;
  rebuild_pages (self);
}

static void
on_category_selected (MockaCategoryList *categories, MockaLauncherView *self)
{
  if (mocka_launcher_view_is_searching (self))
    {
      return;
    }

  show_apps (self, mocka_category_list_get_apps (categories));
}

static void
on_search_changed (GtkSearchEntry *entry, MockaLauncherView *self)
{
  const gchar *text = gtk_entry_get_text (GTK_ENTRY (entry));
  gboolean searching = text != NULL && *text != '\0';
  GPtrArray *results;

  mocka_category_list_set_searching (MOCKA_CATEGORY_LIST (self->categories), searching);

  if (!searching)
    {
      show_apps (self, mocka_category_list_get_apps (MOCKA_CATEGORY_LIST (self->categories)));
      return;
    }

  /* Results in ranking order, favourites first within a rank (SPEC 9.2). */
  results = mocka_search_run (mocka_menu_data_get_all_apps (self->data), text,
                              self->favourites != NULL ? mocka_favourites_get_ids (self->favourites) : NULL);
  show_apps (self, results);
  g_ptr_array_unref (results);
}

/* Rebuilding during a drag would destroy the tile being dragged, and GTK then
 * never emits drag-end for it. */
static void
rerun_search (MockaLauncherView *self)
{
  self->search_pending = FALSE;

  if (mocka_launcher_view_is_searching (self))
    {
      on_search_changed (GTK_SEARCH_ENTRY (self->entry), self);
    }
}

static void
on_favourites_changed (MockaLauncherView *self)
{
  if (mocka_app_drag_is_running ())
    {
      self->search_pending = TRUE;
      return;
    }

  rerun_search (self);
}

static void
on_drag_finished (MockaLauncherView *self)
{
  if (self->search_pending)
    {
      rerun_search (self);
    }
}

static void
on_child_app_activated (MockaLauncherView *self, MockaMenuApp *app)
{
  emit_activated (self, app);
}

static void
on_previous_clicked (GtkWidget *button, MockaLauncherView *self)
{
  set_page (self, self->page > 0 ? self->page - 1 : 0);
}

static void
on_next_clicked (GtkWidget *button, MockaLauncherView *self)
{
  set_page (self, self->page + 1);
}

static gboolean
on_scroll (GtkWidget *widget, GdkEventScroll *event, MockaLauncherView *self)
{
  if (event->direction == GDK_SCROLL_DOWN || event->direction == GDK_SCROLL_RIGHT)
    {
      set_page (self, self->page + 1);
      return GDK_EVENT_STOP;
    }

  if ((event->direction == GDK_SCROLL_UP || event->direction == GDK_SCROLL_LEFT) && self->page > 0)
    {
      set_page (self, self->page - 1);
      return GDK_EVENT_STOP;
    }

  return GDK_EVENT_PROPAGATE;
}

/* Enter launches the selected tile, or the first on the page. */
static void
activate_selected (MockaLauncherView *self)
{
  GtkWidget *page = current_page (self);
  GtkFlowBoxChild *child;
  GList *selected;

  if (page == NULL)
    {
      return;
    }

  child = gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (page), 0);
  selected = gtk_flow_box_get_selected_children (GTK_FLOW_BOX (page));
  if (selected != NULL)
    {
      child = selected->data;
      g_list_free (selected);
    }

  if (child != NULL)
    {
      emit_activated (self, mocka_app_item_get_app (GTK_WIDGET (child)));
    }
}

static void
focus_page (MockaLauncherView *self, gboolean last)
{
  GtkWidget *page = current_page (self);
  GtkFlowBoxChild *child;
  gint index = 0;

  if (page == NULL)
    {
      return;
    }

  if (last)
    {
      GList *children = gtk_container_get_children (GTK_CONTAINER (page));

      index = MAX (0, (gint)g_list_length (children) - 1);
      g_list_free (children);
    }

  child = gtk_flow_box_get_child_at_index (GTK_FLOW_BOX (page), index);
  if (child != NULL)
    {
      gtk_flow_box_select_child (GTK_FLOW_BOX (page), child);
      gtk_widget_grab_focus (GTK_WIDGET (child));
    }
}

/* Where the selection sits on this page, or -1 when nothing is selected. */
static gint
selected_index (MockaLauncherView *self, gint *count)
{
  GtkWidget *page = current_page (self);
  GList *selected;
  GList *children;
  gint index = -1;

  if (page == NULL)
    {
      return -1;
    }

  children = gtk_container_get_children (GTK_CONTAINER (page));
  *count = (gint)g_list_length (children);
  g_list_free (children);

  selected = gtk_flow_box_get_selected_children (GTK_FLOW_BOX (page));
  if (selected != NULL)
    {
      index = gtk_flow_box_child_get_index (selected->data);
      g_list_free (selected);
    }

  return index;
}

/*
 * The page keys, and the arrow that walks off the edge of a page carrying on
 * to the next one (SPEC section 7). Everything inside a page is the flow
 * box's own doing.
 */
static gboolean
navigate_pages (MockaLauncherView *self, guint keyval)
{
  gint count = 0;
  gint index;

  switch (keyval)
    {
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
      set_page (self, self->page + 1);
      return TRUE;
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
      set_page (self, self->page > 0 ? self->page - 1 : 0);
      return TRUE;
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
      index = selected_index (self, &count);
      if (index < 0 || index + 1 < count || self->page + 1 >= self->page_count)
        {
          return FALSE;
        }
      set_page (self, self->page + 1);
      focus_page (self, FALSE);
      return TRUE;
    case GDK_KEY_Left:
    case GDK_KEY_KP_Left:
      index = selected_index (self, &count);
      if (index != 0 || self->page == 0)
        {
          return FALSE;
        }
      set_page (self, self->page - 1);
      focus_page (self, TRUE);
      return TRUE;
    default:
      return FALSE;
    }
}

/*
 * From the entry, because the grid moves by row and column on its own once a
 * tile has the focus (SPEC section 12.2). The entry also binds Home, End and
 * the Page keys for its cursor, so those have to be caught before it.
 */
static gboolean
on_toplevel_key_press (GtkWidget *toplevel, GdkEventKey *event, MockaLauncherView *self)
{
  if (!gtk_widget_has_focus (self->entry))
    {
      return GDK_EVENT_PROPAGATE;
    }

  switch (event->keyval)
    {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      activate_selected (self);
      return GDK_EVENT_STOP;
    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
      return navigate_pages (self, event->keyval) ? GDK_EVENT_STOP : GDK_EVENT_PROPAGATE;
    case GDK_KEY_Down:
    case GDK_KEY_KP_Down:
    case GDK_KEY_Right:
    case GDK_KEY_KP_Right:
      /* Into the grid, which takes the arrows from there. */
      focus_page (self, FALSE);
      return GDK_EVENT_STOP;
    default:
      return GDK_EVENT_PROPAGATE;
    }
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) the signal fixes this signature
static void
on_hierarchy_changed (GtkWidget *widget, GtkWidget *previous_toplevel, MockaLauncherView *self)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (widget);

  if (self->toplevel_key != 0 && previous_toplevel != NULL)
    {
      g_signal_handler_disconnect (previous_toplevel, self->toplevel_key);
      self->toplevel_key = 0;
    }

  if (gtk_widget_is_toplevel (toplevel))
    {
      self->toplevel_key = g_signal_connect (toplevel, "key-press-event", G_CALLBACK (on_toplevel_key_press), self);
    }
}
// NOLINTEND(bugprone-easily-swappable-parameters)

/* Typing anywhere goes to the search entry (SPEC section 9.1). */
static gboolean
on_key_press (GtkWidget *widget, GdkEventKey *event, MockaLauncherView *self)
{
  if (gtk_widget_has_focus (self->entry))
    {
      return GDK_EVENT_PROPAGATE;
    }

  if (navigate_pages (self, event->keyval))
    {
      return GDK_EVENT_STOP;
    }

  if (!gtk_search_entry_handle_event (GTK_SEARCH_ENTRY (self->entry), (GdkEvent *)event))
    {
      return GDK_EVENT_PROPAGATE;
    }

  gtk_widget_grab_focus (self->entry);
  gtk_editable_set_position (GTK_EDITABLE (self->entry), -1);

  return GDK_EVENT_STOP;
}

/*
 * A press on a list or the entry bubbles up to here after they have had it,
 * so being asked is not the same as being the target. The event's own widget
 * is this one only when the press landed on the background itself
 * (SPEC section 7).
 */
static gboolean
on_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  MockaMenuWindow *window = mocka_menu_window_of (widget);

  if (event->button != GDK_BUTTON_PRIMARY || window == NULL)
    {
      return GDK_EVENT_PROPAGATE;
    }

  if (gtk_get_event_widget ((GdkEvent *)event) != widget)
    {
      return GDK_EVENT_PROPAGATE;
    }

  mocka_menu_window_close (window);

  return GDK_EVENT_STOP;
}

/* The theme's background colour, dimmed where a compositor allows it. */
static gboolean
on_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  GtkAllocation alloc;
  /* Dark enough to read against, for a theme that defines no background. */
  GdkRGBA colour = { 0.1, 0.1, 0.1, 1.0 };

  gtk_widget_get_allocation (widget, &alloc);
  gtk_style_context_lookup_color (context, "theme_bg_color", &colour);

  colour.alpha = gdk_screen_is_composited (gtk_widget_get_screen (widget)) ? BACKGROUND_ALPHA : 1.0;

  cairo_save (cr);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  gdk_cairo_set_source_rgba (cr, &colour);
  cairo_rectangle (cr, 0, 0, alloc.width, alloc.height);
  cairo_fill (cr);
  cairo_restore (cr);

  return GDK_EVENT_PROPAGATE;
}

/*
 * The lists bring their own opaque background, which would sit as a solid
 * panel on top of the dimmed desktop. Only the containers are cleared, so a
 * selected row still draws its highlight.
 */
static void
make_panel_transparent (GtkWidget *widget)
{
  static const gchar css[] = ".mocka-launcher-panel,"
                             ".mocka-launcher-panel scrolledwindow,"
                             ".mocka-launcher-panel viewport,"
                             ".mocka-launcher-panel list { background-color: transparent; }";
  static gboolean loaded;

  if (!loaded)
    {
      GtkCssProvider *provider = gtk_css_provider_new ();

      gtk_css_provider_load_from_data (provider, css, -1, NULL);
      gtk_style_context_add_provider_for_screen (gdk_screen_get_default (), GTK_STYLE_PROVIDER (provider),
                                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      g_object_unref (provider);
      loaded = TRUE;
    }

  gtk_style_context_add_class (gtk_widget_get_style_context (widget), "mocka-launcher-panel");
}

void
mocka_launcher_view_rebuild (MockaLauncherView *self)
{
  g_return_if_fail (MOCKA_IS_LAUNCHER_VIEW (self));

  mocka_category_list_rebuild (MOCKA_CATEGORY_LIST (self->categories));
  mocka_launcher_view_reset (self);
}

void
mocka_launcher_view_reset (MockaLauncherView *self)
{
  g_return_if_fail (MOCKA_IS_LAUNCHER_VIEW (self));

  gtk_entry_set_text (GTK_ENTRY (self->entry), "");
  mocka_category_list_reset (MOCKA_CATEGORY_LIST (self->categories), TRUE);

  show_apps (self, mocka_menu_data_get_all_apps (self->data));
}

void
mocka_launcher_view_set_rollover (MockaLauncherView *self, gboolean rollover)
{
  g_return_if_fail (MOCKA_IS_LAUNCHER_VIEW (self));
  mocka_category_list_set_rollover (MOCKA_CATEGORY_LIST (self->categories), rollover);
}

gboolean
mocka_launcher_view_is_searching (MockaLauncherView *self)
{
  const gchar *text;

  g_return_val_if_fail (MOCKA_IS_LAUNCHER_VIEW (self), FALSE);

  text = gtk_entry_get_text (GTK_ENTRY (self->entry));
  return text != NULL && *text != '\0';
}

void
mocka_launcher_view_clear_search (MockaLauncherView *self)
{
  g_return_if_fail (MOCKA_IS_LAUNCHER_VIEW (self));

  gtk_entry_set_text (GTK_ENTRY (self->entry), "");
  mocka_category_list_grab_focus_on_row (MOCKA_CATEGORY_LIST (self->categories));
}

static GtkWidget *
arrow_button (const gchar *icon_name, GCallback handler, MockaLauncherView *self)
{
  GtkWidget *button = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_DND);

  gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  /* Always in the layout, holding its place; update_arrows decides whether it
   * can be seen or used. */
  gtk_widget_show_all (button);
  g_signal_connect (button, "clicked", handler, self);

  return button;
}

static void
mocka_launcher_view_init (MockaLauncherView *self)
{
  gtk_event_box_set_visible_window (GTK_EVENT_BOX (self), TRUE);

  self->columns = GRID_COLUMNS;
  self->rows = GRID_ROWS;

  self->pages = gtk_stack_new ();
  gtk_stack_set_transition_type (GTK_STACK (self->pages), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
  gtk_stack_set_homogeneous (GTK_STACK (self->pages), TRUE);

  self->previous = arrow_button ("go-previous-symbolic", G_CALLBACK (on_previous_clicked), self);
  self->next = arrow_button ("go-next-symbolic", G_CALLBACK (on_next_clicked), self);

  self->dots = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GRID_SPACING);
  gtk_widget_set_halign (self->dots, GTK_ALIGN_CENTER);
  gtk_widget_set_no_show_all (self->dots, TRUE);

  self->entry = gtk_search_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->entry), _ ("Search"));
  gtk_entry_set_width_chars (GTK_ENTRY (self->entry), SEARCH_WIDTH);
  gtk_widget_set_halign (self->entry, GTK_ALIGN_CENTER);

  gtk_widget_add_events (GTK_WIDGET (self), GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

  g_signal_connect (self->entry, "search-changed", G_CALLBACK (on_search_changed), self);
  g_signal_connect (self, "key-press-event", G_CALLBACK (on_key_press), self);
  g_signal_connect (self, "hierarchy-changed", G_CALLBACK (on_hierarchy_changed), self);
  g_signal_connect (self, "button-press-event", G_CALLBACK (on_button_press), NULL);
  g_signal_connect (self, "scroll-event", G_CALLBACK (on_scroll), self);
  g_signal_connect (self, "draw", G_CALLBACK (on_draw), NULL);
  g_signal_connect (self, "size-allocate", G_CALLBACK (on_size_allocate), self);
}

static void
mocka_launcher_view_finalize (GObject *object)
{
  MockaLauncherView *self = MOCKA_LAUNCHER_VIEW (object);

  g_clear_pointer (&self->apps, g_ptr_array_unref);

  G_OBJECT_CLASS (mocka_launcher_view_parent_class)->finalize (object);
}

static void
mocka_launcher_view_class_init (MockaLauncherViewClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = mocka_launcher_view_finalize;

  signals[SIGNAL_APP_ACTIVATED] = g_signal_new ("app-activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL,
                                                NULL, NULL, G_TYPE_NONE, 1, MOCKA_TYPE_MENU_APP);
}

GtkWidget *
mocka_launcher_view_new (MockaMenuData *data, MockaFavourites *favourites)
{
  MockaLauncherView *self = g_object_new (MOCKA_TYPE_LAUNCHER_VIEW, NULL);
  GtkWidget *across = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, EDGE_PADDING);
  GtkWidget *contents = gtk_box_new (GTK_ORIENTATION_VERTICAL, EDGE_PADDING);
  GtkWidget *grid_area = gtk_box_new (GTK_ORIENTATION_VERTICAL, GRID_SPACING);
  GtkWidget *with_arrows = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, GRID_SPACING);
  GtkWidget *column;
  GtkWidget *session;

  self->data = data;
  self->favourites = favourites;
  self->column = column = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  /* Favourites above, session controls at the bottom (SPEC sections 8 and 10). */
  if (favourites != NULL)
    {
      GtkWidget *favourites_column = mocka_favourites_column_new (favourites);

      mocka_favourites_column_set_icon_size (MOCKA_FAVOURITES_COLUMN (favourites_column), COLUMN_ICON_SIZE);
      make_panel_transparent (favourites_column);
      gtk_box_pack_start (GTK_BOX (column), favourites_column, TRUE, TRUE, 0);
      g_signal_connect_swapped (favourites_column, "app-activated", G_CALLBACK (on_child_app_activated), self);
      g_signal_connect_swapped (favourites, "changed", G_CALLBACK (on_favourites_changed), self);
      g_signal_connect_object (mocka_app_drag_get (), "finished", G_CALLBACK (on_drag_finished), self,
                               G_CONNECT_SWAPPED);
    }
  session = mocka_session_bar_new (data);
  /* The Settings shortcut lives there now (SPEC section 10). */
  g_signal_connect_swapped (session, "app-activated", G_CALLBACK (on_child_app_activated), self);
  gtk_box_pack_end (GTK_BOX (column), session, FALSE, FALSE, 0);

  /* One row beneath the search (SPEC section 7). */
  self->categories = mocka_category_list_new (data, GTK_ORIENTATION_HORIZONTAL);
  make_panel_transparent (self->categories);
  g_signal_connect (self->categories, "category-selected", G_CALLBACK (on_category_selected), self);

  /* An arrow on each side of the pages, the dots underneath. */
  gtk_box_pack_start (GTK_BOX (with_arrows), self->previous, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (with_arrows), self->pages, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (with_arrows), self->next, FALSE, FALSE, 0);
  gtk_widget_set_halign (with_arrows, GTK_ALIGN_CENTER);
  gtk_box_pack_start (GTK_BOX (grid_area), with_arrows, TRUE, TRUE, 0);
  gtk_box_pack_start (GTK_BOX (grid_area), self->dots, FALSE, FALSE, 0);
  gtk_widget_set_valign (with_arrows, GTK_ALIGN_CENTER);

  /* Search, categories and grid, one under the other. */
  gtk_box_pack_start (GTK_BOX (contents), self->entry, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (contents), self->categories, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (contents), grid_area, TRUE, TRUE, 0);

  /* The favourites down the far left, the full height (SPEC section 7). */
  gtk_box_pack_start (GTK_BOX (across), column, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (across), contents, TRUE, TRUE, 0);
  gtk_container_set_border_width (GTK_CONTAINER (across), EDGE_PADDING);
  gtk_container_add (GTK_CONTAINER (self), across);

  mocka_launcher_view_reset (self);

  return GTK_WIDGET (self);
}