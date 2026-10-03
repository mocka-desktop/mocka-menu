/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <mate-panel-applet.h>

#include "classic-view.h"
#include "launch.h"
#include "menu-data.h"
#include "menu-window.h"
#include "mocka-menu.h"

#define MOCKA_MENU_FACTORY_ID "MockaMenuAppletFactory"
#define MOCKA_MENU_APPLET_ID "MockaMenuApplet"

#define MIN_ICON_SIZE 16

/* Between the icon and the label, when the label is shown. */
#define LABEL_SPACING 6

#define MOCKA_TYPE_MENU_APPLET (mocka_menu_applet_get_type ())
G_DECLARE_FINAL_TYPE (MockaMenuApplet, mocka_menu_applet, MOCKA, MENU_APPLET, MatePanelApplet)

struct _MockaMenuApplet
{
  MatePanelApplet parent_instance;

  GtkWidget *button;
  GtkWidget *image;
  GtkWidget *label;
  GtkWidget *menu_window;
  GtkWidget *view;
  MockaMenuData *data;
  GSettings *settings; /* shared by every applet, not owned (SPEC section 15) */
};

G_DEFINE_TYPE (MockaMenuApplet, mocka_menu_applet, PANEL_TYPE_APPLET)

/*
 * Tried in order when the icon in the settings is missing from the theme, so
 * an incomplete theme still shows something (SPEC section 16). Qogir-Dark, for
 * one, has no plain start-here.
 */
static const gchar *icon_fallbacks[] = {
  "start-here",
  "open-menu",
  "applications-other",
  "application-x-executable",
};

/* The name to ask GTK for: the wanted one, or the first fallback the theme has. */
static gchar *
resolve_icon_name (const gchar *wanted)
{
  GtkIconTheme *theme = gtk_icon_theme_get_default ();

  if (wanted != NULL && *wanted != '\0' && gtk_icon_theme_has_icon (theme, wanted))
    {
      return g_strdup (wanted);
    }

  for (gsize i = 0; i < G_N_ELEMENTS (icon_fallbacks); i++)
    {
      if (gtk_icon_theme_has_icon (theme, icon_fallbacks[i]))
        {
          return g_strdup (icon_fallbacks[i]);
        }
    }

  return g_strdup ("image-missing");
}

/* A vertical panel has no room for a label (SPEC section 3). */
static gboolean
orient_is_vertical (MatePanelAppletOrient orient)
{
  return orient == MATE_PANEL_APPLET_ORIENT_LEFT || orient == MATE_PANEL_APPLET_ORIENT_RIGHT;
}

/*
 * The room the theme gives the button's padding and border across the panel,
 * so the icon fits inside the button whatever the theme sets.
 */
static gint
button_frame_size (MockaMenuApplet *self)
{
  GtkStyleContext *context = gtk_widget_get_style_context (self->button);
  GtkStateFlags state = gtk_style_context_get_state (context);
  MatePanelAppletOrient orient = mate_panel_applet_get_orient (MATE_PANEL_APPLET (self));
  GtkBorder padding;
  GtkBorder border;

  gtk_style_context_get_padding (context, state, &padding);
  gtk_style_context_get_border (context, state, &border);

  if (orient_is_vertical (orient))
    {
      return padding.left + padding.right + border.left + border.right;
    }

  return padding.top + padding.bottom + border.top + border.bottom;
}

static void
update_icon (MockaMenuApplet *self)
{
  gchar *wanted = g_settings_get_string (self->settings, "icon-name");
  gchar *icon_name = resolve_icon_name (wanted);
  /* Signed on purpose: the frame is subtracted before the floor applies. */
  gint size = (gint)mate_panel_applet_get_size (MATE_PANEL_APPLET (self));

  /* M1 gives the icon its own sizing rules, with HiDPI. */
  gtk_image_set_from_icon_name (GTK_IMAGE (self->image), icon_name, GTK_ICON_SIZE_MENU);
  gtk_image_set_pixel_size (GTK_IMAGE (self->image), MAX (MIN_ICON_SIZE, size - button_frame_size (self)));

  g_free (icon_name);
  g_free (wanted);
}

static void
on_change_size (MockaMenuApplet *self, gint size, gpointer user_data)
{
  update_icon (self);
}

/* The orient says which way the applet faces: UP on a bottom panel. */
static GtkPositionType
panel_side_for_orient (MatePanelAppletOrient orient)
{
  switch (orient)
    {
    case MATE_PANEL_APPLET_ORIENT_DOWN:
      return GTK_POS_TOP;
    case MATE_PANEL_APPLET_ORIENT_LEFT:
      return GTK_POS_RIGHT;
    case MATE_PANEL_APPLET_ORIENT_RIGHT:
      return GTK_POS_LEFT;
    case MATE_PANEL_APPLET_ORIENT_UP:
    default:
      return GTK_POS_BOTTOM;
    }
}

static void
update_label (MockaMenuApplet *self)
{
  MatePanelAppletOrient orient = mate_panel_applet_get_orient (MATE_PANEL_APPLET (self));
  gboolean wanted = g_settings_get_boolean (self->settings, "label-visible");
  gchar *text = g_settings_get_string (self->settings, "label-text");

  /* Empty means the translated default (SPEC section 15). */
  gtk_label_set_text (GTK_LABEL (self->label), (text != NULL && *text != '\0') ? text : _ ("Menu"));
  gtk_widget_set_visible (self->label, wanted && !orient_is_vertical (orient));

  g_free (text);
}

static void
on_change_orient (MockaMenuApplet *self, MatePanelAppletOrient orient, gpointer user_data)
{
  mocka_menu_window_set_panel_side (MOCKA_MENU_WINDOW (self->menu_window), panel_side_for_orient (orient));
  update_label (self);
  /* The frame is measured across the panel, which turns with it. */
  update_icon (self);
}

/* The button shows as pressed while the menu is open (SPEC section 3). */
static void
on_menu_opened (MockaMenuApplet *self)
{
  gtk_widget_set_state_flags (self->button, GTK_STATE_FLAG_ACTIVE, FALSE);
}

static void
on_menu_closed (MockaMenuApplet *self)
{
  gtk_widget_unset_state_flags (self->button, GTK_STATE_FLAG_ACTIVE);
}

static void
on_button_clicked (MockaMenuApplet *self)
{
  mocka_menu_window_toggle (MOCKA_MENU_WINDOW (self->menu_window), self->button);
}

static void
update_rollover (MockaMenuApplet *self)
{
  if (self->view == NULL)
    {
      return;
    }

  mocka_classic_view_set_rollover (MOCKA_CLASSIC_VIEW (self->view),
                                   g_settings_get_boolean (self->settings, "rollover"));
}

/*
 * The search entry sits above or below the menu. In the Classic layout
 * `panel` behaves as `top` until the panel entry arrives in M5
 * (SPEC section 9.3).
 */
static void
update_search_position (MockaMenuApplet *self)
{
  gchar *value;

  if (self->view == NULL)
    {
      return;
    }

  value = g_settings_get_string (self->settings, "search-position");
  mocka_classic_view_set_search_position (MOCKA_CLASSIC_VIEW (self->view), g_strcmp0 (value, "bottom") == 0
                                                                               ? MOCKA_CLASSIC_SEARCH_BOTTOM
                                                                               : MOCKA_CLASSIC_SEARCH_TOP);
  g_free (value);
}

/* Every opening starts from the same state (SPEC section 4). */
static void
on_menu_reset (MockaMenuApplet *self)
{
  if (self->view != NULL)
    {
      mocka_classic_view_reset (MOCKA_CLASSIC_VIEW (self->view));
    }
}

/*
 * Escape clears the search first, and only closes the menu once it is empty
 * (SPEC section 12.2). The window asks before acting on Escape itself.
 */
static gboolean
on_menu_escape (MockaMenuApplet *self)
{
  if (self->view == NULL || !mocka_classic_view_is_searching (MOCKA_CLASSIC_VIEW (self->view)))
    {
      return FALSE;
    }

  mocka_classic_view_clear_search (MOCKA_CLASSIC_VIEW (self->view));
  return TRUE;
}

static void
on_app_activated (MockaMenuApplet *self, MockaMenuApp *app)
{
  /* The menu closes as soon as something is launched (SPEC section 4). */
  mocka_menu_window_close (MOCKA_MENU_WINDOW (self->menu_window));
  mocka_launch_app (mocka_menu_app_get_app_info (app), GTK_WIDGET (self));
}

/* Reads the menu tree once at startup (SPEC section 18). */
static void
build_menu_contents (MockaMenuApplet *self)
{
  GtkWidget *content;
  GError *error = NULL;

  self->data = mocka_menu_data_new ();
  if (!mocka_menu_data_load (self->data, &error))
    {
      g_warning ("mocka-menu: could not read the menu: %s", error->message);
      g_clear_error (&error);
      return;
    }

  self->view = mocka_classic_view_new (self->data);
  g_signal_connect_swapped (self->view, "app-activated", G_CALLBACK (on_app_activated), self);

  content = mocka_menu_window_get_content_area (MOCKA_MENU_WINDOW (self->menu_window));
  gtk_box_pack_start (GTK_BOX (content), self->view, TRUE, TRUE, 0);
  gtk_widget_show_all (self->view);

  update_rollover (self);
  g_signal_connect_swapped (self->settings, "changed::rollover", G_CALLBACK (update_rollover), self);

  update_search_position (self);
  g_signal_connect_swapped (self->settings, "changed::search-position", G_CALLBACK (update_search_position), self);
  g_signal_connect_swapped (self->menu_window, "escape", G_CALLBACK (on_menu_escape), self);
}

static void
mocka_menu_applet_dispose (GObject *object)
{
  MockaMenuApplet *self = MOCKA_MENU_APPLET (object);

  g_clear_pointer (&self->menu_window, gtk_widget_destroy);
  g_clear_object (&self->data);

  G_OBJECT_CLASS (mocka_menu_applet_parent_class)->dispose (object);
}

static void
mocka_menu_applet_init (MockaMenuApplet *self)
{
}

static void
mocka_menu_applet_class_init (MockaMenuAppletClass *klass)
{
  G_OBJECT_CLASS (klass)->dispose = mocka_menu_applet_dispose;
}

static void
mocka_menu_applet_setup (MockaMenuApplet *self)
{
  MatePanelApplet *applet = MATE_PANEL_APPLET (self);
  GtkWidget *box;

  self->settings = mocka_menu_get_settings ();

  self->image = gtk_image_new ();
  self->label = gtk_label_new (NULL);
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, LABEL_SPACING);
  gtk_box_pack_start (GTK_BOX (box), self->image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), self->label, FALSE, FALSE, 0);

  self->button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (self->button), GTK_RELIEF_NONE);
  gtk_container_add (GTK_CONTAINER (self->button), box);
  gtk_container_add (GTK_CONTAINER (self), self->button);

  gtk_widget_set_tooltip_text (self->button, _ ("Menu"));

  /* Left click toggles the menu (SPEC section 3). */
  self->menu_window = mocka_menu_window_new ();
  mocka_menu_window_set_panel_side (MOCKA_MENU_WINDOW (self->menu_window),
                                    panel_side_for_orient (mate_panel_applet_get_orient (applet)));
  g_signal_connect_swapped (self->button, "clicked", G_CALLBACK (on_button_clicked), self);
  g_signal_connect (self, "change-orient", G_CALLBACK (on_change_orient), NULL);
  g_signal_connect_swapped (self->menu_window, "reset", G_CALLBACK (on_menu_reset), self);
  g_signal_connect_swapped (self->menu_window, "opened", G_CALLBACK (on_menu_opened), self);
  g_signal_connect_swapped (self->menu_window, "closed", G_CALLBACK (on_menu_closed), self);
  g_signal_connect_swapped (self->settings, "changed::label-visible", G_CALLBACK (update_label), self);
  g_signal_connect_swapped (self->settings, "changed::label-text", G_CALLBACK (update_label), self);
  build_menu_contents (self);

  mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);

  g_signal_connect_object (self->settings, "changed::icon-name", G_CALLBACK (update_icon), self, G_CONNECT_SWAPPED);
  g_signal_connect (self, "change-size", G_CALLBACK (on_change_size), NULL);

  /* Icons follow the theme, including a theme change (SPEC section 16). */
  g_signal_connect_object (gtk_icon_theme_get_default (), "changed", G_CALLBACK (update_icon), self, G_CONNECT_SWAPPED);
  /* A new GTK theme can change the button's padding and border. */
  g_signal_connect_swapped (self->button, "style-updated", G_CALLBACK (update_icon), self);

  update_icon (self);
  gtk_widget_show_all (GTK_WIDGET (self));
  /* After show_all, which would otherwise reveal a label meant to be hidden. */
  update_label (self);
}

static gboolean
mocka_menu_applet_factory (MatePanelApplet *applet, const gchar *iid, gpointer user_data)
{
  if (strcmp (iid, MOCKA_MENU_APPLET_ID) != 0)
    {
      return FALSE;
    }

  mocka_menu_init ();
  mocka_menu_applet_setup (MOCKA_MENU_APPLET (applet));
  return TRUE;
}

/*
 * In process, like the dock: the panel does not pass drag and drop on to
 * applets running in their own process, and apps are dragged out of the menu
 * (SPEC sections 8 and 13). Confirmed in M0 on a real panel, where a drag
 * started inside the applet reached the desktop, a panel, and Mocka Dock.
 */
MATE_PANEL_APPLET_IN_PROCESS_FACTORY (MOCKA_MENU_FACTORY_ID, MOCKA_TYPE_MENU_APPLET, "Mocka Menu",
                                      mocka_menu_applet_factory, NULL)
