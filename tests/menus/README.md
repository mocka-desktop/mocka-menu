# Test menu tree

The unit tests load this tree instead of the user's real menu, through
`XDG_CONFIG_DIRS` (`xdg/`) and `XDG_DATA_DIRS` (`share/`).

`Exec` must name a program that exists in `PATH`. GIO rejects a desktop entry
whose `Exec` program it cannot find, and a rejected entry disappears from the
tree with no warning. Each app here uses a different real program, so the
search tests of M2 can tell them apart by program name.
