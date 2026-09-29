/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2026 The Mocka Desktop Project
 */

/* SPEC section 6 placement, as geometry: no panel and no display needed. */

#include "classic-view.h"

#define WANT_W MOCKA_CLASSIC_WANT_WIDTH
#define WANT_H MOCKA_CLASSIC_WANT_HEIGHT

/* A 1920x1080 monitor with a 40px panel, and a button at its left end. */
static const GdkRectangle monitor_hd = { 0, 0, 1920, 1080 };

static void
test_bottom_panel (void)
{
  GdkRectangle anchor = { 0, 1040, 40, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &monitor_hd,
                                               GTK_POS_BOTTOM, WANT_W, WANT_H);

  g_assert_cmpint (got.width, ==, WANT_W);
  g_assert_cmpint (got.height, ==, WANT_H);
  g_assert_cmpint (got.x, ==, 0);
  /* Sitting on the button, opening upwards, never over the panel. */
  g_assert_cmpint (got.y + got.height, ==, anchor.y);
}

static void
test_top_panel (void)
{
  GdkRectangle anchor = { 0, 0, 40, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &monitor_hd,
                                               GTK_POS_TOP, WANT_W, WANT_H);

  g_assert_cmpint (got.y, ==, anchor.y + anchor.height);
  g_assert_cmpint (got.x, ==, 0);
}

/* On a vertical panel it opens beside the panel, not above or below it. */
static void
test_left_panel (void)
{
  GdkRectangle anchor = { 0, 500, 40, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &monitor_hd,
                                               GTK_POS_LEFT, WANT_W, WANT_H);

  g_assert_cmpint (got.x, ==, anchor.x + anchor.width);
  g_assert_cmpint (got.y, ==, anchor.y);
}

static void
test_right_panel (void)
{
  GdkRectangle anchor = { 1880, 500, 40, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &monitor_hd,
                                               GTK_POS_RIGHT, WANT_W, WANT_H);

  g_assert_cmpint (got.x + got.width, ==, anchor.x);
  g_assert_cmpint (got.y, ==, anchor.y);
}

/* A button near an edge must not push the window off the monitor. */
static void
test_stays_on_monitor (void)
{
  GdkRectangle anchor = { 1900, 1040, 20, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &monitor_hd,
                                               GTK_POS_BOTTOM, WANT_W, WANT_H);

  g_assert_cmpint (got.x, >=, monitor_hd.x);
  g_assert_cmpint (got.x + got.width, <=, monitor_hd.x + monitor_hd.width);
  g_assert_cmpint (got.y, >=, monitor_hd.y);
}

/* The monitor holding the button, not the first one. */
static void
test_second_monitor (void)
{
  GdkRectangle second = { 1920, 0, 1280, 1024 };
  GdkRectangle anchor = { 1920, 984, 40, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &second,
                                               GTK_POS_BOTTOM, WANT_W, WANT_H);

  g_assert_cmpint (got.x, >=, second.x);
  g_assert_cmpint (got.x + got.width, <=, second.x + second.width);
  g_assert_cmpint (got.y, >=, second.y);
}

/* Too little room above the button, so the menu is shorter. */
static void
test_small_monitor (void)
{
  GdkRectangle small = { 0, 0, 800, 400 };
  GdkRectangle anchor = { 0, 360, 40, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &small,
                                               GTK_POS_BOTTOM, WANT_W, WANT_H);

  g_assert_cmpint (got.height, <, WANT_H);
  g_assert_cmpint (got.height, ==, 360);
  g_assert_cmpint (got.y, ==, 0);
}

/*
 * The floor applies per side. Here there are only 160px above the button, so
 * the height stops at the floor and the window covers the panel rather than
 * shrinking away to nothing. The width has 320px of room, which is over the
 * floor, so it simply fills the monitor.
 */
static void
test_never_below_minimum (void)
{
  GdkRectangle tiny = { 0, 0, 320, 200 };
  GdkRectangle anchor = { 0, 160, 40, 40 };
  GdkRectangle got = mocka_classic_view_place (&anchor, &tiny,
                                               GTK_POS_BOTTOM, WANT_W, WANT_H);

  g_assert_cmpint (got.height, ==, MOCKA_CLASSIC_MIN_HEIGHT);
  g_assert_cmpint (got.width, ==, tiny.width);
  g_assert_cmpint (got.x, ==, 0);
  g_assert_cmpint (got.y, ==, 0);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/classic-placement/bottom-panel", test_bottom_panel);
  g_test_add_func ("/classic-placement/top-panel", test_top_panel);
  g_test_add_func ("/classic-placement/left-panel", test_left_panel);
  g_test_add_func ("/classic-placement/right-panel", test_right_panel);
  g_test_add_func ("/classic-placement/stays-on-monitor", test_stays_on_monitor);
  g_test_add_func ("/classic-placement/second-monitor", test_second_monitor);
  g_test_add_func ("/classic-placement/small-monitor", test_small_monitor);
  g_test_add_func ("/classic-placement/never-below-minimum", test_never_below_minimum);

  return g_test_run ();
}
