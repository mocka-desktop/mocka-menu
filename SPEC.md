# Mocka Menu Specification

Status: draft 1
Component: `mocka-menu`
License: BSD-3-Clause

## About this document

This document describes the behavior of Mocka Menu, an application menu for
the MATE panel. It is the reference for the implementation and was written
before any code.

It was written from the Mocka project's own design decisions, from observed
behavior and public documentation of existing application menus, and from
public specifications (freedesktop.org Desktop Entry, Desktop Menu, EWMH,
X11). It contains no code, code structure, artwork, or stylesheets from any
other project. Mocka Menu is an independent implementation.

## 1. Overview

Mocka Menu is a MATE panel applet that opens an application menu. It has two
layouts:

- **Classic**: a menu window attached to the panel button, with a favourites
  column, a category list, and an app list.
- **Launcher**: a full-screen app grid, with the same favourites column and
  category list.

Both layouts share the same search, favourites, session controls, app menu,
and settings. The layout is a setting and can be changed at any time.

Platform: X11, FreeBSD first. Wayland is out of scope.

## 2. Terms

- **App**: an application, identified by its desktop entry (`.desktop` file)
  ID.
- **Menu tree**: the applications menu as defined by the freedesktop.org
  Desktop Menu specification and MATE's menu files, including the user's own
  menu edits.
- **Category**: a top-level directory of the menu tree, such as Internet or
  Office.
- **Favourite**: an app the user has pinned to the favourites column.
- **Menu window**: the window that opens when the menu is shown, in either
  layout.
- **Panel button**: the applet's button on the panel.

## 3. Panel button

- Shows an icon and an optional label. The label can be hidden or set to any
  text. Default: icon with no label.
- Works on any MATE panel, on any side of the screen, at any panel size. On a
  vertical panel the label is hidden automatically.
- Left click toggles the menu window.
- While the menu window is open, the button shows as pressed.
- Right click shows the button menu (section 11.2).

## 4. Opening and closing

### Opening

- Left click on the panel button.
- Pressing and releasing Super alone (section 12.1).
- Typing into the panel search entry, when search is in the panel
  (section 9.3).

### Closing

The menu window closes when:

- An app, favourite, or session action is launched.
- Escape is pressed with an empty search (section 12.2).
- The user clicks outside the menu window.
- The panel button is clicked again, or Super is pressed and released again.
- Another window takes focus.

### State on each opening

Every time the menu opens it starts from the same state:

- Search is empty and has keyboard focus.
- The "All" category is selected.
- The app list or grid is scrolled to the top.
- Nothing is left over from the previous opening.

### Window behavior

- The menu window grabs the pointer and keyboard while open, as a popup menu
  does, and releases them when it closes.
- It sets the EWMH window type that fits its role (popup menu for Classic,
  a full-screen window for Launcher) and never appears in taskbars, pagers,
  or window switchers.

## 5. Menu data

- Apps and categories come from the menu tree, read through libmate-menu, so
  the user's menu edits (made with the menu editor) are respected.
- Hidden entries (`NoDisplay=true`, `Hidden=true`) and entries excluded by
  `OnlyShowIn` or `NotShowIn` for the current desktop are not shown.
- Settings and administration tools from MATE's settings menu are listed in
  the Preferences and Administration categories.
- An app that appears in more than one place in the menu tree is listed once
  in "All" and in search results.
- Within a category and in "All", apps are sorted by displayed name, using
  the user's locale.
- Names, comments, and category names are shown in the user's language when
  the desktop entry provides a translation.

## 6. Classic layout

### Placement

- The menu window opens next to the panel button, on the side facing the
  screen, and stays within the monitor that contains the button.
- It sizes itself to fit the monitor: a default size, reduced when the
  monitor is too small.
- On vertical panels it opens beside the panel instead of above or below.

### Structure

From left to right:

1. **Favourites column** (section 8), with the session controls at its bottom
   (section 10).
2. **Category list**: "All" first, then the categories of the menu tree in
   menu order.
3. **App list**: one row per app, with icon and name. Hovering shows the
   app's comment as a tooltip.

The search entry spans the top or the bottom of the window, following the
search position setting (section 9.3).

### Categories

- Clicking a category shows its apps.
- With rollover enabled, hovering a category selects it. Off by default.
- While search text is present, the category selection is ignored and the
  app list shows search results (section 9).
- The category list scrolls when it does not fit, and shows a scrollbar only
  when needed.

## 7. Launcher layout

### Placement

- The menu window covers the whole monitor that contains the panel button,
  panels included.
- With a compositor, the background is the theme's background color with
  partial transparency, so the desktop shows through dimmed. Without one, the
  background is solid.
- Clicking empty background closes the Launcher.

### Structure

From left to right:

1. **Favourites column** (section 8), with the session controls at its bottom
   (section 10).
2. **Category list**, the same as in Classic, with "All" selected by default.
3. **App grid**: large icons with the name below. Long names wrap to two
   lines, then end with an ellipsis. Hovering shows the app's comment as a
   tooltip.

The search entry is centered at the top of the screen (section 9.3).

### Grid

- The number of columns follows the available width.
- The grid scrolls vertically when it does not fit.
- While search text is present, the grid shows search results in ranking
  order (section 9.2).

## 8. Favourites

### Column

- A column of favourite apps, on the left of the menu window in both
  layouts.
- Each favourite shows its icon. Hovering shows its name as a tooltip.
- Clicking a favourite launches it.
- Favourites are not a category and do not appear in the category list.
- The column scrolls when it does not fit above the session controls.

### Adding and removing

- "Pin to Favourites" in the app menu (section 11.1).
- Dragging an app from the app list or grid onto the column. It is inserted
  where it is dropped.
- "Unpin from Favourites" in the app menu, available from the favourites
  column, the app list or grid, and search results.

### Reordering

- Dragging a favourite within the column moves it.

### Storage

- Favourites are stored as an ordered list of desktop entry IDs
  (`favourite-apps`, section 15).
- IDs of apps that are not installed are skipped and kept in the list, so a
  favourite returns when its app is reinstalled.
- Changes made by other programs appear immediately.

## 9. Search

### 9.1 Behavior

- Typing anywhere in the menu window goes to the search entry.
- Results update on every keystroke.
- Search covers every app in the menu tree, whatever category is selected.
- Matching is case-insensitive and ignores accents.
- Each app appears once in the results.
- Enter launches the first result, or the selected one after the user moves
  the selection.
- Clearing the search returns to the selected category.
- When nothing matches, the list or grid shows "No results".

### 9.2 Matching and ranking

An app matches when the search text is found in any of these fields. Results
are ranked by the first field that matches, in this order:

1. Name, starting with the search text.
2. Name, a word starting with the search text.
3. Name, containing the search text.
4. Generic name, starting with or containing the search text.
5. Keywords (the desktop entry's `Keywords`). For example, "video" finds a
   video player that lists it as a keyword.
6. The program the app runs (file name of `TryExec`, or of the first word of
   `Exec`).
7. Comment, containing the search text.

Within the same rank, favourites come first, then apps sorted by name.

### 9.3 Search position

The `search-position` setting has three values:

| Value | Classic | Launcher |
|---|---|---|
| `top` | Search entry above the menu content | Search entry at the top of the screen |
| `bottom` | Search entry below the menu content | Search entry at the top of the screen |
| `panel` | Search entry in the panel, beside the button | Search entry in the panel, beside the button |

In `panel` mode:

- The applet shows a search entry on the panel, next to the panel button,
  like the Windows taskbar search. The menu window then has no search entry
  of its own.
- Clicking the entry, or opening the menu with Super, gives the entry
  keyboard focus.
- Typing in the entry opens the menu window if it is closed and shows the
  results in it.
- Up and Down in the entry move the selection in the results. Enter launches
  the selected result.
- On a vertical panel there is no room for an entry, so the menu behaves as
  in `top` mode until the panel is horizontal again.

## 10. Session controls

At the bottom of the favourites column, in both layouts, from top to bottom:

| Button | Action |
|---|---|
| Lock | Lock the screen through the screensaver (`org.mate.ScreenSaver.Lock`) |
| Switch User | Switch to the display manager's greeter (`org.freedesktop.DisplayManager.Seat.SwitchToGreeter` on the seat in `XDG_SEAT_PATH`) |
| Log Out | Ask the session manager to log out, with its confirmation dialog (`org.gnome.SessionManager.Logout`, normal mode) |
| Shut Down | Ask the session manager to show its shutdown dialog (`org.gnome.SessionManager.Shutdown`) |

- Session management goes through mate-session-manager, which uses
  ConsoleKit2 on GhostBSD and FreeBSD. The menu never talks to ConsoleKit2,
  logind, or the display manager's power functions directly.
- All calls are asynchronous. The menu window closes as soon as the button is
  clicked, without waiting for the call to finish.
- A button whose service is not available is hidden. For example, Switch User
  is hidden when there is no display manager seat.
- Each button shows its name as a tooltip.

## 11. Menus

All menus are standard menus drawn by the theme.

### 11.1 App menu (right click on an app)

Available on apps in the app list or grid, in search results, and in the
favourites column. From top to bottom:

1. The actions listed in the app's desktop entry, all of them (for example,
   a browser's private window).
2. Separator, shown only when there are actions.
3. Pin to Favourites, or Unpin from Favourites.
4. Pin to Dock, or Unpin from Dock. Shown only when Mocka Dock is installed
   (section 13).
5. Add to Desktop, or Remove from Desktop.

Rules:

- Add to Desktop copies the app's desktop entry into the user's desktop
  folder (XDG `DESKTOP` user directory) and makes the copy executable, so the
  file manager runs it as a launcher.
- Remove from Desktop deletes only a copy that Mocka Menu recognizes as a
  copy of this app's entry. It never deletes other files.
- Choosing an item closes the app menu but keeps the menu window open, so
  the user can continue.

### 11.2 Button menu (right click on the panel button)

- Preferences
- Edit Menus (opens MATE's menu editor, hidden when it is not installed)
- About
- The panel's standard items: Move, Lock to Panel, Remove from Panel

Preferences opens a preferences window owned by Mocka Menu, so no setting
requires dconf or gsettings. Changes apply immediately, including switching
the layout while the menu is closed.

## 12. Keyboard

### 12.1 Super key

- Pressing and releasing Super alone toggles the menu.
- If any other key or a mouse button is pressed while Super is held, the
  menu does not open. This keeps every Super shortcut working, including
  Mocka Dock's Super + number shortcuts.
- The menu opens on release, not on press.
- Caps Lock, Num Lock, and Scroll Lock have no effect on the shortcut.
- The key is a setting (`hot-key`, section 15). Default: Super.
- If the key is already taken by another program, the menu works without it.
- With several Mocka Menu applets, the first one that successfully grabs the
  key owns it. When it is removed, another takes over.

### 12.2 In the menu window

| Key | Result |
|---|---|
| Typing | Goes to the search entry |
| Up, Down | Move the selection in the list, or by row in the grid |
| Left, Right | Move the selection by column in the grid |
| Page Up, Page Down | Move the selection by one page |
| Home, End | Select the first or last item |
| Tab, Shift + Tab | Move focus between search, favourites, categories, apps, and session controls |
| Up, Down in the category list | Select the previous or next category, and show its apps |
| Enter | Launch the selected app, or run the selected action |
| Menu key, Shift + F10 | Show the app menu of the selected app |
| Escape | Clear the search. With an empty search, close the menu |

Keyboard focus is always visible.

## 13. Mocka Dock integration

- Pin to Dock adds the app to Mocka Dock's shared pinned list
  (`org.mocka_desktop.Dock`, key `pinned-apps`), after the other pinned
  apps. Unpin from Dock removes it. The dock shows the change immediately.
- Pin to Dock and Unpin from Dock are shown only when that settings schema is
  installed.
- Apps can also be dragged from the app list, grid, or favourites column onto
  the dock.

### Shared library

- Mocka Dock's optional menu button (Mocka Dock specification, section 14)
  opens Mocka Menu. To make that possible, Mocka Menu is built as a shared
  library containing everything except the MATE panel applet glue, plus the
  applet itself.
- The library's API is internal to the Mocka project and not stable.
- When the menu is opened from the dock, it behaves exactly as when opened
  from its own panel button, using the same settings.

## 14. Live updates

- The menu follows changes to the menu tree: apps installed or removed, and
  menu edits.
- Changes are collected until no new change has arrived for about 500 ms,
  then applied once. Installing a package with many desktop entries causes a
  single update.
- The new app list is built in memory without touching what is on screen.
- When the menu window is closed, the new list replaces the old one before
  the next opening.
- When the menu window is open, only the difference is applied: new apps
  appear, removed apps disappear, and every other item stays in place. The
  selected category, the search text, the scroll position, and the selection
  are kept.
- If the selected item is removed, the selection moves to the next item.
- The menu window never closes because of an update.

## 15. Settings

All settings are shared by every Mocka Menu applet, in
`org.mocka_desktop.Menu`:

| Key | Meaning | Default |
|---|---|---|
| `layout` | `classic` or `launcher` | `classic` |
| `search-position` | `top`, `bottom`, or `panel` (section 9.3) | `top` |
| `favourite-apps` | Ordered list of favourite desktop entry IDs | See section 20 |
| `label-visible` | Show a label on the panel button | false |
| `label-text` | Label text, empty for the default translated "Menu" | empty |
| `icon-name` | Icon of the panel button | Mocka's menu icon |
| `rollover` | Hovering a category selects it (Classic) | false |
| `hot-key` | Key that toggles the menu | `Super_L` |

All settings are editable in the preferences window (section 11.2).

## 16. Appearance

- The menu is styled with its own CSS classes, built on the current GTK
  theme's colors, so it follows light and dark themes.
- All stylesheets and icons are the Mocka project's own work.
- Icons follow icon theme changes immediately and are sharp on HiDPI
  displays.
- Icons that are missing from the theme fall back to a generic application
  icon.

## 17. Launching apps

- Apps start in the user's home folder.
- Launching uses freedesktop.org Startup Notification, so the launch
  feedback of Mocka Dock and the window manager works.
- If an app fails to start, a message says so. The menu does not freeze.

## 18. Performance requirements

- No periodic polling. The menu only does work in response to events.
- Zero CPU use while the menu is closed and nothing changes.
- The menu tree is read once at startup and then only on changes
  (section 14).
- Both layouts are built once and reused. Opening the menu does not rebuild
  it.
- Icons are cached per size.
- The menu window appears without visible delay after the first opening.
- No interpreter runs in the applet process.

## 19. Dependencies and exclusions

The menu depends only on GTK 3, GLib/GIO, the MATE panel applet library,
libmate-menu, and X11 libraries.

It uses D-Bus only as a client, through GDBus, for the session controls
(section 10). It does not provide any D-Bus service of its own.

It does not use or depend on Keybinder, BAMF, libunity, GVfs, systemd,
logind, or Python.

## 20. Decisions

- The layouts are named Classic and Launcher, in the settings and in the
  interface.
- Favourites are a column on the left in both layouts, not a category.
- Session controls sit at the bottom of the favourites column.
- In the Launcher layout, `top` and `bottom` both place the search entry at
  the top of the screen. `panel` works in both layouts.
- `favourite-apps` defaults to `caja-browser.desktop`, `firefox.desktop`,
  `mate-terminal.desktop`, and `matecc.desktop`, the same apps as Mocka
  Dock's default. Distributions change it with a GSettings override.
- `layout` defaults to `classic`.

## 21. Not included

- A dark theme setting of the menu's own. The menu follows the GTK theme.
- Favourites as a category.
- Showing windows or workspaces in the Launcher.

## 22. Planned for later

- A setting to show session controls as a single power button with a popup
  instead of separate buttons.
- Paged grid with horizontal swiping in the Launcher, as an alternative to
  vertical scrolling.

### Nice to have

- Recently used apps.
- Most used apps.
- File search results.
- Settings search results (individual settings inside Mocka Settings pages).

## 23. Open questions

- Launcher: vertical scrolling for now (section 7). Confirm, or move paging
  from section 22 into the first release.
- Add to Desktop: confirm that an executable copy is enough for Caja on
  GhostBSD to run it without a trust prompt.
