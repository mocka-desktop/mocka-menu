/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

#include "config.h"

#include <gdk/gdkx.h>

#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>

#include "hotkey.h"

/*
 * Ask for a high version, not the lowest that will do. GTK has already
 * negotiated XI2 on this display, and asking for a version below the one a
 * client settled on is a BadValue error, which inside mate-panel takes the
 * whole panel down.
 */
#define XI_ASK_MAJOR 2
#define XI_ASK_MINOR 4

/* XI 2.1 is where raw events started reaching clients through a grab. */
#define XI_NEED_MAJOR 2
#define XI_NEED_MINOR 1

struct _MockaHotkey
{
  GObject parent_instance;

  GdkDisplay *display; /* not owned */
  gint xi_opcode;
  KeySym target; /* NoSymbol when the key is off */

  gboolean armed; /* the key is down */
  gboolean clean; /* nothing else pressed since it went down */
};

G_DEFINE_TYPE (MockaHotkey, mocka_hotkey, G_TYPE_OBJECT)

enum
{
  SIGNAL_ACTIVATED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

/*
 * Level 0 of group 0: the symbol the physical key carries with nothing
 * applied, so Caps Lock, Num Lock and Scroll Lock cannot change it
 * (SPEC section 12.1).
 */
static KeySym
bare_keysym (Display *display, gint keycode)
{
  return XkbKeycodeToKeysym (display, (KeyCode)keycode, 0, 0);
}

static void
on_other_input (MockaHotkey *self)
{
  if (self->armed)
    {
      self->clean = FALSE;
    }
}

static void
handle_raw_key (MockaHotkey *self, XIRawEvent *raw, gboolean pressed)
{
  Display *display = GDK_DISPLAY_XDISPLAY (self->display);
  gboolean is_target = bare_keysym (display, raw->detail) == self->target;

  if (!is_target)
    {
      if (pressed)
        {
          on_other_input (self);
        }
      return;
    }

  if (pressed)
    {
      /* Auto-repeat of the key itself does not spoil it. */
      if (!self->armed)
        {
          self->armed = TRUE;
          self->clean = TRUE;
        }
      return;
    }

  if (self->armed && self->clean)
    {
      g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0);
    }

  self->armed = FALSE;
  self->clean = FALSE;
}

static GdkFilterReturn
on_x_event (GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
  MockaHotkey *self = data;
  XEvent *x_event = xevent;
  XGenericEventCookie *cookie = &x_event->xcookie;
  Display *display = GDK_DISPLAY_XDISPLAY (self->display);
  gboolean fetched_here;

  if (self->target == NoSymbol || cookie->type != GenericEvent || cookie->extension != self->xi_opcode)
    {
      return GDK_FILTER_CONTINUE;
    }

  /*
   * GDK fetches the cookie before the filters run, and a cookie can only be
   * fetched once, so asking again would fail and throw the event away. Only
   * fetch what is not already there, and only free what was fetched here.
   */
  fetched_here = cookie->data == NULL;
  if (fetched_here && !XGetEventData (display, cookie))
    {
      return GDK_FILTER_CONTINUE;
    }

  switch (cookie->evtype)
    {
    case XI_RawKeyPress:
      handle_raw_key (self, cookie->data, TRUE);
      break;
    case XI_RawKeyRelease:
      handle_raw_key (self, cookie->data, FALSE);
      break;
    case XI_RawButtonPress:
      on_other_input (self);
      break;
    default:
      break;
    }

  if (fetched_here)
    {
      XFreeEventData (display, cookie);
    }

  /* Raw events are only ever watched, never claimed. */
  return GDK_FILTER_CONTINUE;
}

static void
mocka_hotkey_finalize (GObject *object)
{
  MockaHotkey *self = MOCKA_HOTKEY (object);

  gdk_window_remove_filter (NULL, on_x_event, self);

  G_OBJECT_CLASS (mocka_hotkey_parent_class)->finalize (object);
}

static void
mocka_hotkey_class_init (MockaHotkeyClass *klass)
{
  G_OBJECT_CLASS (klass)->finalize = mocka_hotkey_finalize;

  signals[SIGNAL_ACTIVATED]
      = g_signal_new ("activated", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

static void
mocka_hotkey_init (MockaHotkey *self)
{
  self->target = NoSymbol;
}

/* Raw key and button events from every master device, on the root window. */
static gboolean
select_raw_events (MockaHotkey *self)
{
  Display *display = GDK_DISPLAY_XDISPLAY (self->display);
  Window root = GDK_WINDOW_XID (gdk_get_default_root_window ());
  unsigned char bits[XIMaskLen (XI_LASTEVENT)] = { 0 };
  XIEventMask mask;
  gint error;

  XISetMask (bits, XI_RawKeyPress);
  XISetMask (bits, XI_RawKeyRelease);
  XISetMask (bits, XI_RawButtonPress);

  mask.deviceid = XIAllMasterDevices;
  mask.mask_len = sizeof (bits);
  mask.mask = bits;

  gdk_x11_display_error_trap_push (self->display);
  XISelectEvents (display, root, &mask, 1);
  XSync (display, False);
  error = gdk_x11_display_error_trap_pop (self->display);

  if (error != 0)
    {
      g_warning ("mocka-menu: could not watch the keyboard, so the menu has no key");
      return FALSE;
    }

  return TRUE;
}

MockaHotkey *
mocka_hotkey_new (void)
{
  MockaHotkey *self;
  GdkDisplay *gdk_display = gdk_display_get_default ();
  Display *display;
  gint major = XI_ASK_MAJOR;
  gint minor = XI_ASK_MINOR;
  gint opcode;
  gint first_event;
  gint first_error;
  gint error;

  if (gdk_display == NULL || !GDK_IS_X11_DISPLAY (gdk_display))
    {
      return NULL;
    }

  display = GDK_DISPLAY_XDISPLAY (gdk_display);
  if (!XQueryExtension (display, "XInputExtension", &opcode, &first_event, &first_error))
    {
      g_warning ("mocka-menu: no XInput extension, so the menu has no key");
      return NULL;
    }

  gdk_x11_display_error_trap_push (gdk_display);
  if (XIQueryVersion (display, &major, &minor) != Success)
    {
      major = 0;
      minor = 0;
    }
  error = gdk_x11_display_error_trap_pop (gdk_display);

  if (error != 0 || major * 10 + minor < XI_NEED_MAJOR * 10 + XI_NEED_MINOR)
    {
      g_warning ("mocka-menu: XInput %d.%d is too old for the menu's key", major, minor);
      return NULL;
    }

  self = g_object_new (MOCKA_TYPE_HOTKEY, NULL);
  self->display = gdk_display;
  self->xi_opcode = opcode;

  if (!select_raw_events (self))
    {
      g_object_unref (self);
      return NULL;
    }

  gdk_window_add_filter (NULL, on_x_event, self);

  return self;
}

void
mocka_hotkey_set_key (MockaHotkey *self, const gchar *keysym_name)
{
  g_return_if_fail (MOCKA_IS_HOTKEY (self));

  self->armed = FALSE;
  self->clean = FALSE;

  if (keysym_name == NULL || *keysym_name == '\0')
    {
      self->target = NoSymbol;
      return;
    }

  self->target = XStringToKeysym (keysym_name);
  if (self->target == NoSymbol)
    {
      g_warning ("mocka-menu: no key called %s", keysym_name);
    }
}