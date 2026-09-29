# Mocka Menu

An application menu for the MATE and Mocka desktops.

Mocka Menu is a MATE panel applet that opens an application menu in one of two
layouts: **Classic**, a menu window attached to the panel button, and
**Launcher**, a full-screen app grid. Both share the same favourites column,
category list, search, session controls, and settings, and the layout is a
setting that can be changed at any time. It targets X11 on FreeBSD and GhostBSD
first.

Mocka Menu is an independent implementation written from its own specification
(see [SPEC.md](SPEC.md)). It shares no code, artwork, or stylesheets with other
menu applets.

## Status

Early development. The specification and the plan are written, the code is not
started yet. The work is split into milestones in [PLAN.md](PLAN.md), from the
skeleton and its risk checks in M0 to release 0.0.1 in M7, with a first alpha for
GhostBSD testers at M5, once both layouts and the preferences window are in.

Planned for 0.0.1:

- Classic and Launcher layouts, sharing everything but their presentation.
- Menu tree read through libmate-menu, so menu edits made with MATE's menu
  editor are respected, along with the Preferences and Administration
  categories.
- Search over names, generic names, keywords, commands, and comments, ranked by
  how well each app matches, ignoring case and accents. The search entry sits
  above the menu, below it, or in the panel beside the button.
- A favourites column in both layouts, filled from the app menu or by dragging
  apps onto it, with session controls at its bottom.
- Pressing and releasing Super alone opens the menu, without grabbing the key,
  so every other Super shortcut keeps working.
- Apps installed or removed while the menu is open appear and disappear without
  losing the selected category, the search text, or the scroll position.
- A preferences window for every setting, so none of them needs `gsettings`.
- Pin to Dock and drag to dock for Mocka Dock, which can also open this menu
  through a shared library.
- English and French translations.

## Building

There is nothing to build yet. From M0 on, the dependencies on FreeBSD and
GhostBSD will be:

```sh
pkg install meson ninja pkgconf gettext-tools gtk3 mate-panel mate-menus \
    libX11 libXi
```

Build, test, and install:

```sh
meson setup build
meson compile -C build
meson test -C build
sudo meson install -C build
```

Then right click a MATE panel, choose "Add to Panel", and pick Mocka Menu.

The menu runs inside mate-panel, so after installing a new build restart the
panel with `mate-panel --replace &`.

## License

BSD-3-Clause. See [LICENSE](LICENSE).
