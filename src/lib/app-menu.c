/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <glib/gi18n-lib.h>

#include "app-menu.h"
#include "launch.h"
#include "menu-window.h"
#include "pins.h"

typedef struct
{
  MockaMenuApp *app;           /* owned */
  MockaFavourites *favourites; /* not owned */
  GtkWidget *over;             /* not owned */
  MockaMenuWindow *window;     /* weak, NULL outside a menu window */
} Target;

static void
target_free (gpointer data)
{
  Target *target = data;

  if (target->window != NULL)
    {
      g_object_remove_weak_pointer (G_OBJECT (target->window), (gpointer *)&target->window);
    }

  g_object_unref (target->app);
  g_free (target);
}

static Target *
target_of (GtkWidget *item)
{
  return g_object_get_data (G_OBJECT (gtk_widget_get_parent (item)), "target");
}

static void
on_action (GtkWidget *item, gpointer user_data)
{
  Target *target = target_of (item);

  mocka_launch_action (mocka_menu_app_get_app_info (target->app), g_object_get_data (G_OBJECT (item), "action"),
                       target->over);

  /* An action is a launch, so the window closes (SPEC section 11.1). */
  if (target->window != NULL)
    {
      mocka_menu_window_close (target->window);
    }
}

static void
on_pin_favourite (GtkWidget *item, gpointer user_data)
{
  Target *target = target_of (item);

  mocka_favourites_add (target->favourites, mocka_menu_app_get_id (target->app), -1);
}

static void
on_unpin_favourite (GtkWidget *item, gpointer user_data)
{
  Target *target = target_of (item);

  mocka_favourites_remove (target->favourites, mocka_menu_app_get_id (target->app));
}

static void
on_pin_dock (GtkWidget *item, gpointer user_data)
{
  mocka_pins_dock_add (mocka_menu_app_get_id (target_of (item)->app));
}

static void
on_unpin_dock (GtkWidget *item, gpointer user_data)
{
  mocka_pins_dock_remove (mocka_menu_app_get_id (target_of (item)->app));
}

static void
on_add_desktop (GtkWidget *item, gpointer user_data)
{
  Target *target = target_of (item);
  GError *error = NULL;

  if (!mocka_pins_desktop_add (mocka_menu_app_get_app_info (target->app), &error))
    {
      g_warning ("mocka-menu: %s was not added to the desktop: %s", mocka_menu_app_get_id (target->app),
                 error->message);
      g_error_free (error);
    }
}

static void
on_remove_desktop (GtkWidget *item, gpointer user_data)
{
  Target *target = target_of (item);
  GError *error = NULL;

  if (!mocka_pins_desktop_remove (mocka_menu_app_get_app_info (target->app), &error))
    {
      g_warning ("mocka-menu: %s was not removed from the desktop: %s", mocka_menu_app_get_id (target->app),
                 error->message);
      g_error_free (error);
    }
}

static void
add_item (GtkWidget *menu, const gchar *label, GCallback handler)
{
  GtkWidget *item = gtk_menu_item_new_with_label (label);

  g_signal_connect (item, "activate", handler, NULL);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

static gboolean
destroy_menu (gpointer menu)
{
  gtk_widget_destroy (GTK_WIDGET (menu));
  g_object_unref (menu);

  return G_SOURCE_REMOVE;
}

/* From an idle, so the chosen item outlives its own handler. */
static void
on_selection_done (GtkWidget *menu, gpointer user_data)
{
  Target *target = g_object_get_data (G_OBJECT (menu), "target");

  if (target->window != NULL)
    {
      mocka_menu_window_end_grab_handover (target->window);
    }

  g_idle_add (destroy_menu, g_object_ref (menu));
}

static void
add_actions (GtkWidget *menu, GDesktopAppInfo *info)
{
  const gchar *const *actions = g_desktop_app_info_list_actions (info);

  for (const gchar *const *at = actions; *at != NULL; at++)
    {
      gchar *name = g_desktop_app_info_get_action_name (info, *at);
      GtkWidget *item = gtk_menu_item_new_with_label (name);

      g_object_set_data_full (G_OBJECT (item), "action", g_strdup (*at), g_free);
      g_signal_connect (item, "activate", G_CALLBACK (on_action), NULL);
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
      g_free (name);
    }

  if (actions[0] != NULL)
    {
      gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_separator_menu_item_new ());
    }
}

void
mocka_app_menu_popup (MockaMenuApp *app, MockaFavourites *favourites, GtkWidget *over, const GdkEvent *event)
{
  GtkWidget *menu;
  Target *target;
  GDesktopAppInfo *info;
  const gchar *id;

  g_return_if_fail (MOCKA_IS_MENU_APP (app));
  g_return_if_fail (MOCKA_IS_FAVOURITES (favourites));
  g_return_if_fail (GTK_IS_WIDGET (over));

  info = mocka_menu_app_get_app_info (app);
  id = mocka_menu_app_get_id (app);
  menu = gtk_menu_new ();

  target = g_new0 (Target, 1);
  target->app = g_object_ref (app);
  target->favourites = favourites;
  target->over = over;
  target->window = mocka_menu_window_of (over);
  if (target->window != NULL)
    {
      g_object_add_weak_pointer (G_OBJECT (target->window), (gpointer *)&target->window);
    }
  g_object_set_data_full (G_OBJECT (menu), "target", target, target_free);

  add_actions (menu, info);

  if (mocka_favourites_contains (favourites, id))
    {
      add_item (menu, _ ("Unpin from Favourites"), G_CALLBACK (on_unpin_favourite));
    }
  else
    {
      add_item (menu, _ ("Pin to Favourites"), G_CALLBACK (on_pin_favourite));
    }

  if (mocka_pins_dock_available ())
    {
      if (mocka_pins_dock_contains (id))
        {
          add_item (menu, _ ("Unpin from Dock"), G_CALLBACK (on_unpin_dock));
        }
      else
        {
          add_item (menu, _ ("Pin to Dock"), G_CALLBACK (on_pin_dock));
        }
    }

  if (mocka_pins_desktop_has_copy (info))
    {
      add_item (menu, _ ("Remove from Desktop"), G_CALLBACK (on_remove_desktop));
    }
  else
    {
      add_item (menu, _ ("Add to Desktop"), G_CALLBACK (on_add_desktop));
    }

  gtk_menu_attach_to_widget (GTK_MENU (menu), over, NULL);
  gtk_widget_show_all (menu);
  g_signal_connect (menu, "selection-done", G_CALLBACK (on_selection_done), NULL);

  /* The menu takes the pointer the window is holding. */
  if (target->window != NULL)
    {
      mocka_menu_window_begin_grab_handover (target->window);
    }

  if (event != NULL)
    {
      gtk_menu_popup_at_pointer (GTK_MENU (menu), event);
    }
  else
    {
      gtk_menu_popup_at_widget (GTK_MENU (menu), over, GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
    }
}