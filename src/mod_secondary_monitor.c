// RSMods: start Rocksmith on a secondary monitor (LaunchOnExternalMonitor.cpp). With SecondaryMonitor = on the game's
// window moves, once, as soon as it is there, to [Mod Settings] SecondaryMonitorXPosition, SecondaryMonitorYPosition: the
// top left corner of the screen to use, as Windows has it (from the top left of the main screen, down). RSMods' app fills
// these in from the screen picked; the Mac app does the same, in points.
//
// RS Re-ARMed's window: a titled window, or in full screen a borderless one the size of the screen (rsp_shell.m,
// goFullScreen). A window goes with its top left corner to the point; a full screen one fills the screen the point is on.
// AppKit wants the main thread: the move is sent there.
#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdatomic.h>
#include <stdlib.h>

#include <CoreGraphics/CGGeometry.h>

#include "rsmods.h"

typedef struct {
  double x, y;  // top left, y down from the top of the main screen
} target;

static atomic_int g_moved;  // sent (the main thread sets it back if there was nothing to move yet)

static id msg(id obj, const char *sel) { return ((id (*)(id, SEL))objc_msgSend)(obj, sel_registerName(sel)); }
static unsigned long msg_ul(id obj, const char *sel) {
  return ((unsigned long (*)(id, SEL))objc_msgSend)(obj, sel_registerName(sel));
}
static CGRect frame_of(id obj) { return ((CGRect (*)(id, SEL))objc_msgSend)(obj, sel_registerName("frame")); }
static id at(id array, unsigned long i) {
  return ((id (*)(id, SEL, unsigned long))objc_msgSend)(array, sel_registerName("objectAtIndex:"), i);
}

// The game's window: the largest visible one
static id game_window(void) {
  id app = msg((id)objc_getClass("NSApplication"), "sharedApplication");
  id windows = app ? msg(app, "windows") : nil;
  unsigned long n = windows ? msg_ul(windows, "count") : 0;
  id best = nil;
  double area = 0;
  for (unsigned long i = 0; i < n; i++) {
    id w = at(windows, i);
    if (!((BOOL (*)(id, SEL))objc_msgSend)(w, sel_registerName("isVisible"))) continue;
    CGRect f = frame_of(w);
    if (f.size.width * f.size.height > area) area = f.size.width * f.size.height, best = w;
  }
  return best;
}

static void move_window(void *ctx) {
  target *t = ctx;
  id w = game_window();
  id screens = msg((id)objc_getClass("NSScreen"), "screens");
  unsigned long n = screens ? msg_ul(screens, "count") : 0;
  if (!w || !n) {
    rsm_log("secondary monitor: no window or screen yet");
    g_moved = 0;  // try again
    free(t);
    return;
  }
  // AppKit's screen coordinates start at the bottom left of the main screen (the first), y up
  double top = frame_of(at(screens, 0)).size.height;
  CGPoint p = {t->x, top - t->y};
  id screen = nil;
  for (unsigned long i = 0; i < n && !screen; i++) {
    CGRect s = frame_of(at(screens, i));
    if (p.x >= s.origin.x && p.x < s.origin.x + s.size.width && p.y > s.origin.y && p.y <= s.origin.y + s.size.height)
      screen = at(screens, i);
  }
  int full = !(msg_ul(w, "styleMask") & 1);  // not titled: RS Re-ARMed's full screen
  if (full && screen) {
    CGRect s = frame_of(screen);
    ((void (*)(id, SEL, CGRect, BOOL))objc_msgSend)(w, sel_registerName("setFrame:display:"), s, YES);
    rsm_log("secondary monitor: full screen on the screen at %.0f, %.0f (%.0f x %.0f)", t->x, t->y, s.size.width,
            s.size.height);
  } else {
    ((void (*)(id, SEL, CGPoint))objc_msgSend)(w, sel_registerName("setFrameTopLeftPoint:"), p);
    rsm_log("secondary monitor: window at %.0f, %.0f%s", t->x, t->y, screen ? "" : " (on no screen)");
  }
  free(t);
}

static void tick(void) {
  if (g_moved || !rsm_on("SecondaryMonitor") || !*rsm_menu()) return;
  target *t = malloc(sizeof *t);
  if (!t) return;
  t->x = (double)rsm_long("SecondaryMonitorXPosition", 0);
  t->y = (double)rsm_long("SecondaryMonitorYPosition", 0);
  g_moved = 1;
  dispatch_async_f(dispatch_get_main_queue(), t, move_window);
}

const rsm_mod rsm_mod_secondary_monitor = {"secondary_monitor", tick, NULL, NULL, NULL};
