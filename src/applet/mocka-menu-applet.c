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
#include "favourites.h"
#include "hotkey.h"
#include "launch.h"
#include "launcher-view.h"
#include "menu-data.h"
#include "menu-window.h"
#include "mocka-menu.h"

#define MOCKA_MENU_FACTORY_ID "MockaMenuAppletFactory"
#define MOCKA_MENU_APPLET_ID "MockaMenuApplet"

#define MIN_ICON_SIZE 16

/* Between the icon and the label, when the label is shown. */
#define LABEL_SPACING 6
#define PANEL_ENTRY_CHARS 16

#define MOCKA_TYPE_MENU_APPLET (mocka_menu_applet_get_type ())
G_DECLARE_FINAL_TYPE (MockaMenuApplet, mocka_menu_applet, MOCKA, MENU_APPLET, MatePanelApplet)

struct _MockaMenuApplet
{
  MatePanelApplet parent_instance;

  GtkWidget *button;
  GtkWidget *image;
  GtkWidget *label;
  GtkWidget *panel_entry;
  GtkWidget *menu_window;
  GtkWidget *view;
  MockaMenuData *data;
  MockaFavourites *favourites;
  MockaHotkey *hotkey;
  GSettings *settings;      /* shared by every applet, not owned (SPEC section 15) */
  GtkWidget *watched_panel; /* the toplevel whose clicks close the menu, not owned */
  gboolean rebuild_when_closed;
};

G_DEFINE_TYPE (MockaMenuApplet, mocka_menu_applet, PANEL_TYPE_APPLET)

static void update_search_position (MockaMenuApplet *self);
static void drop_entry_focus (MockaMenuApplet *self);

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
  update_search_position (self);
  update_label (self);
  /* The frame is measured across the panel, which turns with it. */
  update_icon (self);
}

/* The button shows as pressed while the menu is open (SPEC section 3). */
static void
on_menu_opened (MockaMenuApplet *self)
{
  gtk_widget_set_state_flags (self->button, GTK_STATE_FLAG_ACTIVE, FALSE);

  /* Nothing grabs the keyboard in `panel`, so the entry has to hold it or the
   * typing would land in whichever window the panel took it from. */
  if (gtk_widget_get_visible (self->panel_entry))
    {
      mate_panel_applet_request_focus (MATE_PANEL_APPLET (self), gtk_get_current_event_time ());
      gtk_widget_grab_focus (self->panel_entry);
      gtk_editable_set_position (GTK_EDITABLE (self->panel_entry), -1);
    }
}

static void
rebuild_view (MockaMenuApplet *self)
{
  if (MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      mocka_launcher_view_rebuild (MOCKA_LAUNCHER_VIEW (self->view));
    }
  else if (MOCKA_IS_CLASSIC_VIEW (self->view))
    {
      mocka_classic_view_rebuild (MOCKA_CLASSIC_VIEW (self->view));
    }
}

static void
on_menu_closed (MockaMenuApplet *self)
{
  gtk_widget_unset_state_flags (self->button, GTK_STATE_FLAG_ACTIVE);
  gtk_entry_set_text (GTK_ENTRY (self->panel_entry), "");
  drop_entry_focus (self);

  if (self->rebuild_when_closed)
    {
      self->rebuild_when_closed = FALSE;
      rebuild_view (self);
    }
}

/*
 * A hidden window keeps the style it was last shown with, so the next opening
 * would measure the old font. Rebuilding needs the menu closed, so an open one
 * waits until it is.
 */
static void
rebuild_view_when_closed (MockaMenuApplet *self)
{
  if (mocka_menu_window_is_open (MOCKA_MENU_WINDOW (self->menu_window)))
    {
      self->rebuild_when_closed = TRUE;
      return;
    }

  rebuild_view (self);
}

/*
 * Each row settles on its icon as it is built, so icons that arrived after
 * the menu was last built, a newly installed game among them, need the rows
 * made again (SPEC section 16).
 */
static void
on_icon_theme_changed (MockaMenuApplet *self)
{
  update_icon (self);
  rebuild_view_when_closed (self);
}

/* Closed, the whole list is replaced; open, only the difference moves
 * (SPEC section 14). */
static void
on_tree_changed (MockaMenuApplet *self)
{
  if (!mocka_menu_window_is_open (MOCKA_MENU_WINDOW (self->menu_window)))
    {
      rebuild_view (self);
      return;
    }

  if (MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      mocka_launcher_view_update (MOCKA_LAUNCHER_VIEW (self->view));
    }
  else if (MOCKA_IS_CLASSIC_VIEW (self->view))
    {
      mocka_classic_view_update (MOCKA_CLASSIC_VIEW (self->view));
    }
}

static void
send_search_text (MockaMenuApplet *self, const gchar *text)
{
  if (MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      mocka_launcher_view_set_search_text (MOCKA_LAUNCHER_VIEW (self->view), text);
    }
  else if (MOCKA_IS_CLASSIC_VIEW (self->view))
    {
      mocka_classic_view_set_search_text (MOCKA_CLASSIC_VIEW (self->view), text);
    }
}

/* The panel keeps giving the entry the keyboard until something takes it. */
static void
drop_entry_focus (MockaMenuApplet *self)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));

  if (gtk_widget_is_toplevel (toplevel) && gtk_window_get_focus (GTK_WINDOW (toplevel)) == self->panel_entry)
    {
      gtk_window_set_focus (GTK_WINDOW (toplevel), NULL);
    }
}

/*
 * In `panel` the menu takes no GTK grab, so clicks on the panel go to the
 * panel rather than to the menu. A click anywhere but the entry and the
 * button closes the menu and gives the keyboard back (SPEC section 4).
 */
static gboolean
on_panel_click (MockaMenuApplet *self, GdkEventButton *event)
{
  GtkWidget *clicked = gtk_get_event_widget ((GdkEvent *)event);

  if (!gtk_widget_get_visible (self->panel_entry))
    {
      return GDK_EVENT_PROPAGATE;
    }

  if (clicked != NULL
      && (clicked == self->panel_entry || gtk_widget_is_ancestor (clicked, self->panel_entry) || clicked == self->button
          || gtk_widget_is_ancestor (clicked, self->button)))
    {
      return GDK_EVENT_PROPAGATE;
    }

  mocka_menu_window_close (MOCKA_MENU_WINDOW (self->menu_window));
  drop_entry_focus (self);

  return GDK_EVENT_PROPAGATE;
}

static void
watch_panel_clicks (MockaMenuApplet *self)
{
  GtkWidget *toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));

  if (!gtk_widget_is_toplevel (toplevel) || toplevel == self->watched_panel)
    {
      return;
    }

  self->watched_panel = toplevel;
  gtk_widget_add_events (toplevel, GDK_BUTTON_PRESS_MASK);
  g_signal_connect_object (toplevel, "button-press-event", G_CALLBACK (on_panel_click), self, G_CONNECT_SWAPPED);
}

/*
 * The panel does not hand keyboard focus to an applet on its own, so a click
 * in the entry asks for it first, or there is no cursor to type into.
 */
static gboolean
on_panel_entry_button_press (MockaMenuApplet *self, GdkEventButton *event)
{
  mate_panel_applet_request_focus (MATE_PANEL_APPLET (self), event->time);
  gtk_widget_grab_focus (self->panel_entry);

  return GDK_EVENT_PROPAGATE;
}

static void
on_panel_entry_changed (MockaMenuApplet *self)
{
  const gchar *text = gtk_entry_get_text (GTK_ENTRY (self->panel_entry));

  /* Typing opens the menu to show the results (SPEC section 9.3). The menu
   * takes the grab as it opens, so the cursor is put back in the entry. */
  if (*text != '\0' && !mocka_menu_window_is_open (MOCKA_MENU_WINDOW (self->menu_window)))
    {
      mocka_menu_window_open (MOCKA_MENU_WINDOW (self->menu_window), self->button);
      gtk_widget_grab_focus (self->panel_entry);
      gtk_editable_set_position (GTK_EDITABLE (self->panel_entry), -1);
    }

  send_search_text (self, text);
}

static gboolean
on_panel_entry_key_press (MockaMenuApplet *self, GdkEventKey *event)
{
  if (event->keyval == GDK_KEY_Escape)
    {
      if (*gtk_entry_get_text (GTK_ENTRY (self->panel_entry)) != '\0')
        {
          gtk_entry_set_text (GTK_ENTRY (self->panel_entry), "");
          return GDK_EVENT_STOP;
        }

      mocka_menu_window_close (MOCKA_MENU_WINDOW (self->menu_window));
      return GDK_EVENT_STOP;
    }

  if (MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      return mocka_launcher_view_navigate_results (MOCKA_LAUNCHER_VIEW (self->view), event->keyval);
    }

  if (MOCKA_IS_CLASSIC_VIEW (self->view))
    {
      return mocka_classic_view_navigate_results (MOCKA_CLASSIC_VIEW (self->view), event->keyval);
    }

  return GDK_EVENT_PROPAGATE;
}

static void
on_button_clicked (MockaMenuApplet *self)
{
  mocka_menu_window_toggle (MOCKA_MENU_WINDOW (self->menu_window), self->button);
}

/* Super alone opens the menu, as the setting names it (SPEC section 12.1). */
static void
update_hotkey (MockaMenuApplet *self)
{
  gchar *key;

  if (self->hotkey == NULL)
    {
      return;
    }

  key = g_settings_get_string (self->settings, "hot-key");
  mocka_hotkey_set_key (self->hotkey, key);
  g_free (key);
}

static void
on_hotkey_activated (MockaMenuApplet *self)
{
  mocka_menu_window_toggle (MOCKA_MENU_WINDOW (self->menu_window), self->button);
}

static void
update_rollover (MockaMenuApplet *self)
{
  /* Classic only, as the setting says (SPEC section 15). */
  if (self->view == NULL || MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      return;
    }

  mocka_classic_view_set_rollover (MOCKA_CLASSIC_VIEW (self->view),
                                   g_settings_get_boolean (self->settings, "rollover"));
}

/*
 * The search entry sits above or below the menu, or in the panel beside the
 * button (SPEC section 9.3).
 */
static gboolean
panel_is_horizontal (MockaMenuApplet *self)
{
  MatePanelAppletOrient orient = mate_panel_applet_get_orient (MATE_PANEL_APPLET (self));

  return orient == MATE_PANEL_APPLET_ORIENT_UP || orient == MATE_PANEL_APPLET_ORIENT_DOWN;
}

/* A vertical panel has no room for an entry, so `panel` falls back to `top`
 * until it is horizontal again (SPEC section 9.3). */
static MockaClassicSearchPosition
wanted_search_position (MockaMenuApplet *self)
{
  gchar *value = g_settings_get_string (self->settings, "search-position");
  MockaClassicSearchPosition position = MOCKA_CLASSIC_SEARCH_TOP;

  if (g_strcmp0 (value, "bottom") == 0)
    {
      position = MOCKA_CLASSIC_SEARCH_BOTTOM;
    }
  else if (g_strcmp0 (value, "panel") == 0 && panel_is_horizontal (self))
    {
      position = MOCKA_CLASSIC_SEARCH_PANEL;
    }

  g_free (value);

  return position;
}

static void
update_search_position (MockaMenuApplet *self)
{
  MockaClassicSearchPosition position = wanted_search_position (self);
  gboolean in_panel = position == MOCKA_CLASSIC_SEARCH_PANEL;

  gtk_widget_set_visible (self->panel_entry, in_panel);
  mocka_menu_window_set_keyboard_elsewhere (MOCKA_MENU_WINDOW (self->menu_window), in_panel);

  if (!in_panel)
    {
      gtk_entry_set_text (GTK_ENTRY (self->panel_entry), "");
    }

  if (self->view == NULL)
    {
      return;
    }

  if (MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      mocka_launcher_view_set_search_in_panel (MOCKA_LAUNCHER_VIEW (self->view), in_panel);
      return;
    }

  mocka_classic_view_set_search_position (MOCKA_CLASSIC_VIEW (self->view), position);
}

/* Every opening starts from the same state (SPEC section 4). */
static void
on_menu_reset (MockaMenuApplet *self)
{
  if (self->view == NULL)
    {
      return;
    }

  if (MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      mocka_launcher_view_reset (MOCKA_LAUNCHER_VIEW (self->view));
    }
  else
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
  if (self->view == NULL)
    {
      return FALSE;
    }

  if (MOCKA_IS_LAUNCHER_VIEW (self->view))
    {
      if (!mocka_launcher_view_is_searching (MOCKA_LAUNCHER_VIEW (self->view)))
        {
          return FALSE;
        }

      mocka_launcher_view_clear_search (MOCKA_LAUNCHER_VIEW (self->view));
      return TRUE;
    }

  if (!mocka_classic_view_is_searching (MOCKA_CLASSIC_VIEW (self->view)))
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

static gboolean
launcher_wanted (MockaMenuApplet *self)
{
  gchar *layout = g_settings_get_string (self->settings, "layout");
  gboolean launcher = g_strcmp0 (layout, "launcher") == 0;

  g_free (layout);

  return launcher;
}

/* The layout the setting asks for, packed into the window (SPEC section 15). */
static void
build_view (MockaMenuApplet *self)
{
  GtkWidget *content = mocka_menu_window_get_content_area (MOCKA_MENU_WINDOW (self->menu_window));
  gboolean launcher = launcher_wanted (self);

  mocka_menu_window_set_layout (MOCKA_MENU_WINDOW (self->menu_window),
                                launcher ? MOCKA_MENU_WINDOW_LAUNCHER : MOCKA_MENU_WINDOW_CLASSIC);

  self->view = launcher ? mocka_launcher_view_new (self->data, self->favourites)
                        : mocka_classic_view_new (self->data, self->favourites);
  g_signal_connect_swapped (self->view, "app-activated", G_CALLBACK (on_app_activated), self);

  gtk_box_pack_start (GTK_BOX (content), self->view, TRUE, TRUE, 0);
  gtk_widget_show_all (self->view);

  update_rollover (self);
  update_search_position (self);
}

/*
 * A layout change takes effect while the menu is closed (SPEC section 15), so
 * an open menu is closed first and the old layout destroyed with it.
 */
static void
update_layout (MockaMenuApplet *self)
{
  if (self->view == NULL || MOCKA_IS_LAUNCHER_VIEW (self->view) == launcher_wanted (self))
    {
      return;
    }

  mocka_menu_window_close (MOCKA_MENU_WINDOW (self->menu_window));

  gtk_widget_destroy (self->view);
  self->view = NULL;

  build_view (self);
}

/* Reads the menu tree once at startup (SPEC section 18). */
static void
build_menu_contents (MockaMenuApplet *self)
{
  GError *error = NULL;

  self->data = mocka_menu_data_new ();
  if (!mocka_menu_data_load (self->data, &error))
    {
      g_warning ("mocka-menu: could not read the menu: %s", error->message);
      g_clear_error (&error);
      return;
    }

  self->favourites = mocka_favourites_new (self->data, self->settings);
  build_view (self);

  g_signal_connect_object (self->data, "changed", G_CALLBACK (on_tree_changed), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (gtk_settings_get_default (), "notify::gtk-font-name", G_CALLBACK (rebuild_view_when_closed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings, "changed::layout", G_CALLBACK (update_layout), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings, "changed::rollover", G_CALLBACK (update_rollover), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings, "changed::search-position", G_CALLBACK (update_search_position), self,
                           G_CONNECT_SWAPPED);

  /* Every applet watches the key for now; one owner comes next. */
  self->hotkey = mocka_hotkey_new ();
  if (self->hotkey != NULL)
    {
      g_signal_connect_swapped (self->hotkey, "activated", G_CALLBACK (on_hotkey_activated), self);
      update_hotkey (self);
      g_signal_connect_object (self->settings, "changed::hot-key", G_CALLBACK (update_hotkey), self, G_CONNECT_SWAPPED);
    }
  g_signal_connect_swapped (self->menu_window, "escape", G_CALLBACK (on_menu_escape), self);
}

/* MATE's menu editor, or NULL when it is not installed (SPEC section 11.2). */
static GDesktopAppInfo *
menu_editor (void)
{
  return g_desktop_app_info_new ("mozo.desktop");
}

static void
on_edit_menus (GtkAction *action, gpointer user_data)
{
  GDesktopAppInfo *editor = menu_editor ();

  if (editor == NULL)
    {
      return;
    }

  mocka_launch_app (editor, GTK_WIDGET (user_data));
  g_object_unref (editor);
}

static void
on_about (GtkAction *action, gpointer user_data)
{
  const gchar *authors[] = { "Eric Turgeon", NULL };

  gtk_show_about_dialog (NULL, "program-name", _ ("Mocka Menu"), "version", PACKAGE_VERSION, "comments",
                         _ ("Application menu for the MATE panel"), "logo-icon-name", "start-here", "copyright",
                         "Copyright \xc2\xa9 2026 The Mocka Desktop Project", "license-type", GTK_LICENSE_BSD_3,
                         "authors", authors, "website", "https://github.com/mocka-desktop/mocka-menu", NULL);
}

/*
 * The panel's applet menu, shown on right click on the panel button, with our
 * items above the panel's own (SPEC section 11.2). Preferences arrives with
 * its window in M5.
 */
static void
setup_button_menu (MockaMenuApplet *self)
{
  static const gchar menu_xml[] = "<menuitem name=\"EditMenus\" action=\"EditMenus\" />"
                                  "<menuitem name=\"About\" action=\"About\" />";
  GtkActionGroup *group;
  GDesktopAppInfo *editor = menu_editor ();

  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  static const GtkActionEntry entries[] = {
    { "EditMenus", "preferences-system", N_ ("_Edit Menus"), NULL, NULL, G_CALLBACK (on_edit_menus) },
    { "About", "help-about", N_ ("_About"), NULL, NULL, G_CALLBACK (on_about) },
  };

  group = gtk_action_group_new ("MockaMenuActions");
  gtk_action_group_set_translation_domain (group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions (group, entries, G_N_ELEMENTS (entries), self);

  /* Hidden when the editor is not installed (SPEC section 11.2). */
  gtk_action_set_visible (gtk_action_group_get_action (group, "EditMenus"), editor != NULL);
  G_GNUC_END_IGNORE_DEPRECATIONS

  mate_panel_applet_setup_menu (MATE_PANEL_APPLET (self), menu_xml, group);

  g_clear_object (&editor);
  g_object_unref (group);
}

static void
mocka_menu_applet_dispose (GObject *object)
{
  MockaMenuApplet *self = MOCKA_MENU_APPLET (object);

  g_clear_pointer (&self->menu_window, gtk_widget_destroy);
  g_clear_object (&self->favourites);
  g_clear_object (&self->data);
  g_clear_object (&self->hotkey);

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
  GtkWidget *panel_box;

  self->settings = mocka_menu_get_settings ();

  self->image = gtk_image_new ();
  self->label = gtk_label_new (NULL);
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, LABEL_SPACING);
  gtk_box_pack_start (GTK_BOX (box), self->image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), self->label, FALSE, FALSE, 0);

  self->button = gtk_button_new ();
  gtk_button_set_relief (GTK_BUTTON (self->button), GTK_RELIEF_NONE);
  gtk_container_add (GTK_CONTAINER (self->button), box);

  self->panel_entry = gtk_search_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (self->panel_entry), _ ("Search"));
  gtk_entry_set_width_chars (GTK_ENTRY (self->panel_entry), PANEL_ENTRY_CHARS);
  gtk_widget_set_no_show_all (self->panel_entry, TRUE);

  panel_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, LABEL_SPACING);
  gtk_box_pack_start (GTK_BOX (panel_box), self->button, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (panel_box), self->panel_entry, FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (self), panel_box);

  gtk_widget_set_tooltip_text (self->button, _ ("Menu"));

  /* Left click toggles the menu (SPEC section 3). */
  self->menu_window = mocka_menu_window_new ();
  mocka_menu_window_set_panel_side (MOCKA_MENU_WINDOW (self->menu_window),
                                    panel_side_for_orient (mate_panel_applet_get_orient (applet)));
  g_signal_connect_swapped (self->button, "clicked", G_CALLBACK (on_button_clicked), self);
  g_signal_connect_swapped (self->panel_entry, "search-changed", G_CALLBACK (on_panel_entry_changed), self);
  g_signal_connect_swapped (self->panel_entry, "button-press-event", G_CALLBACK (on_panel_entry_button_press), self);
  g_signal_connect_swapped (self->panel_entry, "key-press-event", G_CALLBACK (on_panel_entry_key_press), self);
  g_signal_connect (self, "change-orient", G_CALLBACK (on_change_orient), NULL);
  g_signal_connect_swapped (self, "hierarchy-changed", G_CALLBACK (watch_panel_clicks), self);
  g_signal_connect_swapped (self, "realize", G_CALLBACK (watch_panel_clicks), self);
  g_signal_connect_swapped (self->menu_window, "reset", G_CALLBACK (on_menu_reset), self);
  g_signal_connect_swapped (self->menu_window, "opened", G_CALLBACK (on_menu_opened), self);
  g_signal_connect_swapped (self->menu_window, "closed", G_CALLBACK (on_menu_closed), self);
  g_signal_connect_object (self->settings, "changed::label-visible", G_CALLBACK (update_label), self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings, "changed::label-text", G_CALLBACK (update_label), self, G_CONNECT_SWAPPED);
  build_menu_contents (self);

  setup_button_menu (self);

  mate_panel_applet_set_flags (applet, MATE_PANEL_APPLET_EXPAND_MINOR);

  g_signal_connect_object (self->settings, "changed::icon-name", G_CALLBACK (update_icon), self, G_CONNECT_SWAPPED);
  g_signal_connect (self, "change-size", G_CALLBACK (on_change_size), NULL);

  /* Icons follow the theme, including a theme change (SPEC section 16). */
  g_signal_connect_object (gtk_icon_theme_get_default (), "changed", G_CALLBACK (on_icon_theme_changed), self,
                           G_CONNECT_SWAPPED);
  /* A new GTK theme can change the button's padding and border. */
  g_signal_connect_swapped (self->button, "style-updated", G_CALLBACK (update_icon), self);

  update_icon (self);
  gtk_widget_show_all (GTK_WIDGET (self));
  watch_panel_clicks (self);
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
