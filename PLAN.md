# Mocka Menu: implementation plan

Behavior is defined in `SPEC.md`. This file covers how it is built and in what
order. Tick items as they are completed.

## Toolchain

C11, built with Clang, the FreeBSD base compiler. Clang is the only supported
compiler. Meson, same project defaults as mocka-dock (`c_std=c11`,
`warning_level=2`, flags added through `cc.get_supported_arguments()` so older
Clang versions do not fail on unknown flags).

## Dependencies

`gtk+-3.0`, `libmatepanelapplet-4.0`, `libmate-menu`, `gio-unix-2.0`, `x11`,
`xi` (XInput 2.1 or later).

## Module layout

| Module | Role |
|---|---|
| `src/applet/mocka-menu-applet.c` | Applet factory, panel button (icon, label, orientation), button menu, panel search entry host |
| `src/lib/menu-data.c` | Menu tree through libmate-menu: apps, categories, "All", deduplication, locale sorting (SPEC section 5) |
| `src/lib/app-model.c` | `GListStore` models for categories and results, change debounce, diff and splice (SPEC section 14) |
| `src/lib/search.c` | Text normalization (case folding, accent removal) and ranking (SPEC section 9.2) |
| `src/lib/menu-window.c` | Shared window logic: open, close, grabs, state reset, window type (SPEC section 4) |
| `src/lib/classic-view.c` | Classic layout (SPEC section 6) |
| `src/lib/launcher-view.c` | Launcher layout (SPEC section 7) |
| `src/lib/favourites.c` | `favourite-apps` storage and the favourites column (SPEC section 8) |
| `src/lib/session.c` | Session controls over GDBus, service availability (SPEC section 10) |
| `src/lib/app-menu.c` | App menu: desktop actions, favourites, dock, desktop (SPEC section 11.1) |
| `src/lib/pins.c` | Dock pinning through `org.mocka_desktop.Dock`, desktop copies (SPEC sections 11.1, 13) |
| `src/lib/hotkey.c` | Super alone through XInput 2 raw events, ownership across applets through an X manager selection (SPEC section 12.1) |
| `src/lib/launch.c` | Launching with startup notification, from the home folder, error message (SPEC section 17) |
| `src/lib/prefs-window.c` | Preferences window (SPEC section 11.2) |
| `data/` | GSettings schema, `.mate-panel-applet` file, CSS and UI files as a GResource |
| `tests/` | Unit tests |
| `tests/menus/` | Test menu trees and desktop entries, loaded through `XDG_CONFIG_DIRS` and `XDG_DATA_DIRS` |

`src/lib/` builds `libmocka-menu`, a shared library with a pkg-config file, so
Mocka Dock's menu button can open the menu (SPEC section 13). Its API is
internal and versioned with the project. `src/applet/` is the MATE panel
applet and is the only code that uses the panel applet API.

Schema: `org.mocka_desktop.Menu` (fixed path, shared by all applets).

### Super key

The menu does not grab Super. It selects XInput 2 raw key and button events on
the root window. Since XI 2.1, raw events reach every client that selects
them, even while another client holds a grab. The menu opens on a Super
release when no other key or button was pressed since the Super press. It never
takes the keyboard away from other programs, so Mocka Dock's Super + number
grabs and marco's bindings keep working.

## M0: Skeleton and risk checks

- [x] New repository `mocka-desktop/mocka-menu`, started from `SPEC.md`, `PLAN.md`, `README.md` with the independent-implementation note, and `LICENSE`. No upstream history
- [x] Meson build, `data/` files, the schema
- [x] gettext setup from the start: `po/` directory, all user-visible strings wrapped in `_()`
- [x] `libmocka-menu` and the applet as separate build targets
- [x] Minimal applet that appears in "Add to Panel" as Mocka Menu
- [x] Decide in-process or out-of-process: verify that dragging an app from a test window in the applet reaches the desktop, a panel, and Mocka Dock. The dock had to move in-process for drag and drop (mocka-dock PLAN.md), so test in-process first
- [x] Verify Super alone through XInput 2 raw events: opens on release, not after Super + key or Super + click, NumLock and CapsLock on or off, marco running with its default bindings
- [x] Verify another client's passive grab keeps firing while we watch raw events: a test client grabbing Super + 1 received it, and the raw watcher saw the same keys and declined to open. Mocka Dock has no Super + number shortcuts yet, so retest against the dock itself when they land (M6)
- [x] Verify a popup window with pointer and keyboard grabs from the applet (Classic)
- [x] Verify a window covering the whole monitor, panels included, under marco (Launcher). Covers the panels, dims the desktop with the compositor and is solid without it. Only one monitor here, so which monitor it lands on is retested when a second one is available
- [x] Verify libmate-menu loads the menu tree on GhostBSD, including the settings menu for Preferences and Administration
- [x] Verify on GhostBSD with LightDM and ConsoleKit2: `org.gnome.SessionManager` (Logout, Shutdown), `org.mate.ScreenSaver` (Lock), and `org.freedesktop.DisplayManager.Seat` (SwitchToGreeter) at `XDG_SEAT_PATH`
- [x] Verify whether an executable desktop entry copied to `~/Desktop` runs in Caja without a trust prompt (SPEC section 23)
- [x] Port skeleton in the GhostBSD ports overlay (draft only, stashed until the alpha). `x11/mocka-menu` in ghostbsd-ports, stashed with `git stash`; needs `make makesum` once the alpha is tagged
- [ ] Manual test by maintainer

## M1: Menu data and Classic layout

- [x] Menu data: categories, "All", deduplication, hidden and `OnlyShowIn`/`NotShowIn` entries, locale sorting, translated names, with unit tests against `tests/menus/`
- [x] Menu window: open and close rules, state reset on each opening, window type (SPEC section 4)
- [x] Classic placement: next to the button, within the monitor, vertical panels, reduced size on small monitors
- [x] Category list and app list, tooltips, scrollbars only when needed
- [ ] Rollover setting
- [ ] Launching: home folder, startup notification, error message (SPEC section 17)
- [ ] Panel button: icon, label, label hidden on vertical panels, pressed state
- [ ] Icons follow the icon theme, HiDPI, fallback icon
- [ ] Manual test by maintainer

## M2: Search, keyboard, and Super key

- [ ] Normalization and ranking, with unit tests for every rank in SPEC section 9.2, accents, and favourites first
- [ ] Search entry at `top` and `bottom`, typing anywhere goes to search, "No results"
- [ ] Keyboard navigation in the menu window (SPEC section 12.2)
- [ ] Super alone through XInput 2 raw events, `hot-key` setting
- [ ] Hotkey ownership across applets: the owner holds an X manager selection, other applets watch it and take over when the owner goes away
- [ ] Manual test by maintainer

## M3: Favourites, session controls, app menu

- [ ] `favourite-apps` storage and live updates, uninstalled apps skipped and kept
- [ ] Favourites column: launch, tooltips, scrolling, drag to add, drag to reorder
- [ ] Session controls: async calls, hidden when the service is missing
- [ ] App menu: desktop actions, Pin to Favourites, Pin to Dock (shown only with the dock schema installed), Add to Desktop and Remove from Desktop, with unit tests for the desktop copy and its recognition
- [ ] Drag apps out to the desktop, a panel, and Mocka Dock
- [ ] Button menu: Edit Menus, About, panel items. Preferences stays hidden until M5; settings are changed with `gsettings` meanwhile
- [ ] Default `favourite-apps` (SPEC section 20)
- [ ] Manual test by maintainer

## M4: Launcher layout

- [ ] Full-monitor window on the monitor of the panel button
- [ ] Transparent background with a compositor, solid without one
- [ ] Favourites column, category list, and app grid with wrapped names
- [ ] Search at the top of the screen for `top` and `bottom`
- [ ] Grid keyboard navigation by row and column
- [ ] Clicking empty background closes
- [ ] `layout` setting, switching applied while the menu is closed
- [ ] Manual test with the compositor on and off
- [ ] Decide vertical scrolling or paging (SPEC section 23)

## M5: Live updates, panel search, preferences (first alpha)

- [ ] Change debounce, background rebuild, swap when closed
- [ ] Diff and splice while open, keeping category, search, scroll, and selection, with unit tests for the diff
- [ ] Manual test: install and remove packages with `pkg` while the menu is open, in both layouts
- [ ] `panel` search position: entry beside the button, focus on click and Super, opening on typing, Up, Down, and Enter, fallback to `top` on vertical panels
- [ ] Preferences window for every setting in SPEC section 15, changes applied immediately
- [ ] Preferences in the button menu
- [ ] Manual test by maintainer
- [ ] Add `x11/mocka-menu` to ghostbsd-ports from the stashed draft, pointing at the alpha
- [ ] GhostBSD override for `favourite-apps` adding `software-station.desktop`, in ghostbsd-mate-settings
- [ ] User guide on the project wiki, first version for testers
- [ ] Alpha release for GhostBSD testers

## M6: Mocka Dock integration

- [ ] `libmocka-menu` installed with its pkg-config file
- [ ] Mocka Dock menu button opens the menu through the library (dock M6)
- [ ] Drop the synthetic Super + number item from mocka-dock PLAN.md M5, since the menu no longer grabs Super
- [ ] Manual test: menu opened from the dock and from the panel button, with the same settings

## M7: Release 0.0.1

- [ ] Translations: English and French (gettext set up in M0)
- [ ] Man page
- [ ] User guide on the wiki brought up to date with everything added since the alpha
- [ ] README update
- [ ] Performance check: RSS, idle CPU, time to show the menu, 500+ desktop entries
- [ ] Tag 0.0.1, update the GhostBSD port, submit `x11/mocka-menu` to FreeBSD ports
