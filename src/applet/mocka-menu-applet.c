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
#define MOCKA_MENU_APPLET_ID  "MockaMenuApplet"

/* Room left around the icon so the button border fits on a thin panel. */
#define BUTTON_PADDING 4
#define MIN_ICON_SIZE  16

#define MOCKA_TYPE_MENU_APPLET (mocka_menu_applet_get_type ())
G_DECLARE_FINAL_TYPE (MockaMenuApplet, mocka_menu_applet, MOCKA, MENU_APPLET,
                      MatePanelApplet)

struct _MockaMenuApplet
{
  MatePanelApplet parent_instance;

  GtkWidget *button;
  GtkWidget *image;
  GtkWidget *menu_window;
  GtkWidget *view;
  MockaMenuData *data;
  GSettings *settings;  /* shared by every applet, not owned (SPEC section 15) */
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

  if (wanted != NULL && *wanted != '\0'
      && gtk_icon_theme_has_icon (theme, wanted))
    return g_strdup (wanted);

  for (gsize i = 0; i < G_N_ELEMENTS (icon_fallbacks); i++)
    {
      if (gtk_icon_theme_has_icon (theme, icon_fallbacks[i]))
        return g_strdup (icon_fallbacks[i]);
    }

  return g_strdup ("image-missing");
}

static void
update_icon (MockaMenuApplet *self)
{
  gchar *wanted = g_settings_get_string (self->settings, "icon-name");
  gchar *icon_name = resolve_icon_name (wanted);
  gint size = mate_panel_applet_get_size (MATE_PANEL_APPLET (self));

  /* M1 gives the icon its own sizing rules, with HiDPI. */
  gtk_image_set_from_icon_name (GTK_IMAGE (self->image), icon_name,
                                GTK_ICON_SIZE_MENU);
  gtk_image_set_pixel_size (GTK_IMAGE (self->image),
                            MAX (MIN_ICON_SIZE, size - BUTTON_PADDING));

  g_free (icon_name);
  g_free (wanted);
}

static void
on_change_size (MockaMenuApplet *self,
                gint             size,
                gpointer         user_data)
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
on_change_orient (MockaMenuApplet       *self,
                  MatePanelAppletOrient  orient,
                  gpointer               user_data)
{
  mocka_menu_window_set_panel_side (MOCKA_MENU_WINDOW (self->menu_window),
                                    panel_side_for_orient (orient));
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
    return;

  mocka_classic_view_set_rollover (MOCKA_CLASSIC_VIEW (self->view),
      g_settings_get_boolean (self->settings, "rollover"));
}

/* Every opening starts from the same state (SPEC section 4). */
static void
on_menu_reset (MockaMenuApplet *self)
{
  if (self->view != NULL)
    mocka_classic_view_reset (MOCKA_CLASSIC_VIEW (self->view));
}

static void
on_app_activated (MockaMenuApplet *self,
                  MockaMenuApp    *app)
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
  g_signal_connect_swapped (self->view, "app-activated",
                            G_CALLBACK (on_app_activated), self);

  content = mocka_menu_window_get_content_area (
      MOCKA_MENU_WINDOW (self->menu_window));
  gtk_box_pack_start (GTK_BOX (content), self->view, TRUE, TRUE, 0);
  gtk_widget_show_all (self->view);

  update_rollover (self);
  g_signal_connect_swapped (self->settings, "changed::rollover",
                            G_CALLBACK (update_rollover), self);
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

  self->settings = mocka_menu_get_settings ();

  self->image = gtk_image_new ();
  self->button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (self->button), GTK_RELIEF_NONE);
  gtk_container_add (GTK_CONTAINER (self->button), self->image);
  gtk_container_add (GTK_CONTAINER (self), self->button);

  gtk_widget_set_tooltip_text (self->button, _("Menu"));

  /* Left click toggles the menu (SPEC section 3). */
  self->menu_window = mocka_menu_window_new ();
  mocka_menu_window_set_panel_side (MOCKA_MENU_WINDOW (self->menu_window),
      panel_side_for_orient (mate_panel_applet_get_orient (applet)));
  g_signal_connect_swapped (self->button, "clicked",
                            G_CALLBACK (on_button_clicked), self);
  g_signal_connect (self, "change-orient", G_CALLBACK (on_change_orient), NULL);
  g_signal_connect_swapped (self->menu_window, "reset",
                            G_CALLBACK (on_menu_reset), self);
  build_menu_contents (self);

  mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);

  g_signal_connect_object (self->settings, "changed::icon-name",
                           G_CALLBACK (update_icon), self, G_CONNECT_SWAPPED);
  g_signal_connect (self, "change-size", G_CALLBACK (on_change_size), NULL);

  /* Icons follow the theme, including a theme change (SPEC section 16). */
  g_signal_connect_object (gtk_icon_theme_get_default (), "changed",
                           G_CALLBACK (update_icon), self, G_CONNECT_SWAPPED);

  update_icon (self);
  gtk_widget_show_all (GTK_WIDGET (self));
}

static gboolean
mocka_menu_applet_factory (MatePanelApplet *applet,
                           const gchar     *iid,
                           gpointer         user_data)
{
  if (strcmp (iid, MOCKA_MENU_APPLET_ID) != 0)
    return FALSE;

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
MATE_PANEL_APPLET_IN_PROCESS_FACTORY (MOCKA_MENU_FACTORY_ID,
                                      MOCKA_TYPE_MENU_APPLET,
                                      "Mocka Menu",
                                      mocka_menu_applet_factory,
                                      NULL)
