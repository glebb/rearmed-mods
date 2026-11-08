// RSMods: toggle the loft (Loft.cpp, LoftMod.cpp): the venue behind the note highway goes black (for streams,
// keyed out with a luma key). RSMods sets the far plane of the loft's camera from 10000 to 1. ToggleLoft = on
// and ToggleLoftWhen: manual (the key toggles it), song (off in songs) or startup (always off). Lessons
// (Guided Experience) keep the loft unless it is manual.
//
// The camera is a Gamebryo NiCamera: view frustum at ni_camera_frustum, far plane at ni_camera_far; the loft's is the
// one with far 10000, as RSMods finds it. The game sets every camera's frustum each frame through
// NiCamera::SetViewFrustum (camera_set_frustum, a hook point), so a far plane written from outside does not last:
// after each SetViewFrustum, a camera whose far plane is 10000 gets 1 while the loft is off. The cameras it did
// that to get 10000 back when the loft comes on (the next SetViewFrustum would also restore them).
// The far plane takes the stage's props; the song's backdrop with Venue Mode off (the "Narnia" stage: fog,
// portal, back wall), the Loft menu's room and the tone's gear are hidden by name in the scene graph
// (rsm_scene_hide). Some cameras (the back wall's) get their frustum once, when the scene is built: every camera
// the hook has seen is kept, and while the loft is off those still at 10000 are set directly too, after a safe
// read shows they are still NiCameras.
//
// RSMods: the green screen wall (GreenScreenWall = on): only the wall behind the stage goes black, the amps and the rest
// stay. RSMods skips the wall's draw (one mesh, textured loft_concrete_wall_b and portal_wall_ao); here the back wall's
// objects are culled, the wall with its trim and wiring (backwall_*). RSMods also takes it for the loft in lessons, which
// keep the loft: there, when the loft would be off, the wall goes instead.
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "rsmods.h"

#define LOFT_FAR 10000.f
#define OFF_FAR 1.f
#define MAX_CAMS 64

static const char *const LESSONS[] = {"GuidedExperience_Game", "GuidedExperience_Pause", NULL};

static atomic_int g_want_off;  // set by the tick, read by the hook (game thread both, but kept simple)
static uint64_t g_cams[MAX_CAMS];
static float g_cam_far[MAX_CAMS];  // each one's far plane before
static int g_ncams;
// Far planes to cut besides 10000 ([Mod Settings] LoftExtraFar, comma separated): this build's scene has the
// back wall under another camera than RSMods' loft camera
#define MAX_EXTRA 8
static float g_extra[MAX_EXTRA];
static int g_nextra;

static int cut(float f) {
  if (f == LOFT_FAR) return 1;
  for (int i = 0; i < g_nextra; i++)
    if (f == g_extra[i]) return 1;
  return 0;
}
static int g_manual_off, g_was_off;

static __thread uint64_t t_cam;  // the camera of the SetViewFrustum running on this thread

static int before_set_frustum(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  t_cam = (uint64_t)(uintptr_t)OV_PTR(a, void);  // this (its register is not preserved across the call)
  return 0;
}

// Every camera seen, once (logged with RS_MODS_LOG: which cameras the scene has and their planes)
#define MAX_SEEN 128
static uint64_t g_seen[MAX_SEEN];
static int g_nseen;

static void log_camera(uint64_t cam) {
  for (int i = 0; i < g_nseen; i++)
    if (g_seen[i] == cam) return;
  if (g_nseen < MAX_SEEN)
    g_seen[g_nseen++] = cam;
  else
    memmove(g_seen, g_seen + 1, sizeof g_seen - sizeof *g_seen), g_seen[MAX_SEEN - 1] = cam;
  float fr[6];
  memcpy(fr, (void *)(uintptr_t)(cam + (uint64_t)RC_OFF(ni_camera_frustum)), sizeof fr);
  rsm_log("loft: camera %llx frustum l %.3f r %.3f t %.3f b %.3f near %.3f far %.1f (menu %s)",
          (unsigned long long)cam, fr[0], fr[1], fr[2], fr[3], fr[4], fr[5], rsm_menu());
}

static void after_set_frustum(rc_ctx *c, void *user) {
  (void)c, (void)user;
  uint64_t cam = t_cam;
  if (!cam) return;
  log_camera(cam);
  if (!atomic_load(&g_want_off)) return;
  float f;
  memcpy(&f, (void *)(uintptr_t)(cam + (uint64_t)RC_OFF(ni_camera_far)), 4);
  if (!cut(f)) return;
  float off = OFF_FAR;
  memcpy((void *)(uintptr_t)(cam + (uint64_t)RC_OFF(ni_camera_far)), &off, 4);
  for (int i = 0; i < g_ncams; i++)
    if (g_cams[i] == cam) return;
  if (g_ncams < MAX_CAMS) {
    g_cam_far[g_ncams] = f;
    g_cams[g_ncams++] = cam;
  }
  rsm_log("loft: off (camera at %llx, far %.1f)", (unsigned long long)cam, f);
}

// The far plane of a seen camera, if it is still a NiCamera (a safe read: it may have been freed)
static int read_far(uint64_t cam, float *far) {
  uint8_t vt[8] = {0};
  mach_vm_size_t got = 0;
  if (mach_vm_read_overwrite(mach_task_self(), cam, (mach_vm_size_t)rc_guest_ptr_size, (mach_vm_address_t)(uintptr_t)vt, &got) ||
      got != (mach_vm_size_t)rc_guest_ptr_size ||
      mach_vm_read_overwrite(mach_task_self(), cam + (uint64_t)RC_OFF(ni_camera_far), 4, (mach_vm_address_t)(uintptr_t)far, &got) ||
      got != 4)
    return 0;
  return (uint64_t)(uintptr_t)rc_mod_ptr(vt) == RC_GA(nicamera_vtable);
}

// Diagnostic (RS_MODS_LOG): every NiCamera in the game's writable memory, with its frustum, to compare with the
// cameras the hook sees. A thread scans; the cameras are read safely.
static void log_scanned_camera(uint64_t cam, int kind, const char *cls, void *ctx) {
  (void)kind;
  int *total = ctx;
  if (strcmp(cls, "NiCamera")) return;
  float fr[6];
  mach_vm_size_t got = 0;
  if (mach_vm_read_overwrite(mach_task_self(), cam + (uint64_t)RC_OFF(ni_camera_frustum), sizeof fr, (mach_vm_address_t)(uintptr_t)fr, &got) ||
      got != sizeof fr)
    return;
  rsm_log("loft scan: camera %llx l %.3f r %.3f t %.3f b %.3f near %.3f far %.1f", (unsigned long long)cam, fr[0],
          fr[1], fr[2], fr[3], fr[4], fr[5]);
  (*total)++;
}

static void *scan_cameras(void *arg) {
  (void)arg;
  int total = 0;
  rsm_scene_each(log_scanned_camera, &total);
  rsm_log("loft scan: %d cameras", total);
  return NULL;
}

// Loft off for the cameras whose frustum is not set every frame
static void cut_camera(uint64_t cam) {
  float f;
  if (!read_far(cam, &f) || !cut(f)) return;
  float off = OFF_FAR;
  memcpy((void *)(uintptr_t)(cam + (uint64_t)RC_OFF(ni_camera_far)), &off, 4);
  int known = 0;
  for (int j = 0; j < g_ncams; j++) known |= g_cams[j] == cam;
  if (!known && g_ncams < MAX_CAMS) {
    g_cam_far[g_ncams] = f;
    g_cams[g_ncams++] = cam;
  }
  rsm_log("loft: off (camera at %llx, far %.1f, set once)", (unsigned long long)cam, f);
}

static void cut_seen(void) {
  for (int i = 0; i < g_nseen; i++) cut_camera(g_seen[i]);
}

static void restore(void) {
  for (int i = 0; i < g_ncams; i++) {
    float f;
    if (!read_far(g_cams[i], &f)) continue;
    if (f == OFF_FAR) {
      f = g_cam_far[i];
      memcpy((void *)(uintptr_t)(g_cams[i] + (uint64_t)RC_OFF(ni_camera_far)), &f, 4);
    }
  }
  g_ncams = 0;
}

static void tick(void) {
  int want_off = 0, wall_off = rsm_on("GreenScreenWall");
  const char *extra = rsm_get("Mod Settings", "LoftExtraFar");
  g_nextra = 0;
  for (const char *p = extra; p && *p && g_nextra < MAX_EXTRA;) {
    char *end;
    float v = strtof(p, &end);
    if (end == p) break;
    g_extra[g_nextra++] = v;
    p = end;
    while (*p == ',' || *p == ' ') p++;
  }
  if (rsm_on("ToggleLoft")) {
    const char *when = rsm_get("Toggle Switches", "ToggleLoftWhen");
    if (!when || !strcasecmp(when, "manual"))
      want_off = g_manual_off;
    else if (rsm_menu_in(LESSONS))
      wall_off = 1;  // the loft stays in lessons, its wall goes
    else if (!strcasecmp(when, "startup"))
      want_off = 1;
    else
      want_off = rsm_menu_in(RSM_SONG_MODES);
  } else {
    g_manual_off = 0;
  }
  if (rsm_sunlight_hides_venue()) want_off = 1;
  atomic_store(&g_want_off, want_off);
  // the venue's props and floor (GRVenueActor*), the tone's gear (amps, cabinets) placed in the venue, and the surface
  // under the strings: the guitar's fret_board and Guitar_NeckBack (the headstock is the Headstock mod's)
  static const char *const BACKDROP[] = {"NarniaFogModel", "NarniaPortalModel", "backwall_*", "loft_root_node",
                                         "GearPlacementNode", "GRVenueActor", "GRVenueActorAlpha", "fret_board",
                                         "Guitar_NeckBack", NULL};
  rsm_scene_hide("loft", BACKDROP, RSM_CULL, want_off);
  static const char *const WALL[] = {"backwall_*", NULL};
  rsm_scene_hide("green_screen_wall", WALL, RSM_CULL, wall_off && !want_off);
  // What is left of the venue's light: the post effects lay a haze around the strings. Songs only: the menus need them.
  static const char *const POST_EFFECTS[] = {"PostEffectsStep", NULL};
  rsm_scene_hide("loft_post_effects", POST_EFFECTS, RSM_STEP_OFF, want_off && rsm_menu_in(RSM_SONG_MODES));
  static unsigned frame;
  if (want_off && ++frame % 30 == 0) cut_seen();  // twice a second
  if (!want_off && g_was_off) {
    restore();
    rsm_log("loft: on");
  }
  g_was_off = want_off;
}

static void key(const rsm_key_event *e) {
  if (e->down || e->keycode != rsm_key("ToggleLoftKey") || !rsm_on("ToggleLoft")) return;
  const char *when = rsm_get("Toggle Switches", "ToggleLoftWhen");
  if (when && strcasecmp(when, "manual")) return;
  g_manual_off = !g_manual_off;
  if (g_manual_off && getenv("RS_MODS_LOG") && *getenv("RS_MODS_LOG") != '0') {  // diagnostic only
    pthread_t t;
    if (pthread_create(&t, NULL, scan_cameras, NULL) == 0) pthread_detach(t);
  }
}

__attribute__((constructor)) static void init(void) {
  if (!RC_HAS_OFF(ni_camera_frustum) || !RC_HAS_OFF(ni_camera_far) || !RC_ADDR(nicamera_vtable)) {
    fprintf(stderr, "[rsmods] the camera's layout isn't known on %s: the loft stays off\n", rc_target());
    return;
  }
  rsm_hook("camera_set_frustum", before_set_frustum, after_set_frustum);
}

const rsm_mod rsm_mod_loft = {"loft", tick, key, NULL, NULL};
