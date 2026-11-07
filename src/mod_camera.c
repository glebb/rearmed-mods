// Camera director: the player's own view of the note highway, on top of the camera the game drives, which still
// follows the song. CameraDirector = on; in a song the zoom, height, side and tilt keys change the view, Ctrl + key the
// other way, the reset key goes back to the game's view. The view is kept in [Mod Settings] (CameraZoom, percent;
// CameraHeight and CameraSide, the scene's units; CameraTilt, degrees) and so in RSMods.ini, which can also be edited
// while the game runs.
//
// The highway has a camera of its own, the venue others, so the stage stays where it is. It's the one with near 1 and
// far 1000 away from the origin; a helper camera has the same planes at the origin. It is the last camera the game
// sets in a frame, and what is written after its SetViewFrustum (camera_set_frustum) is what gets drawn, in both
// editions. The frustum is new with every call and the game works out the world transform every frame, so nothing has
// to be put back: a frame with no view is the game's own. Only a call on a frame the game didn't move the camera finds
// our own transform there, and takes the game's from what it noted. The local transform has no effect: the game writes
// it. The rotation's columns are the camera's direction, up and right: world X is mostly the way it looks, Z is up.
#include <math.h>
#include <stdio.h>

#include "rsmods.h"

#define ZOOM_MIN 60
#define ZOOM_MAX 160
#define ZOOM_STEP 5
#define HEIGHT_MIN (-10)
#define HEIGHT_MAX 20
#define HEIGHT_STEP 1
#define SIDE_MIN (-20)
#define SIDE_MAX 20
#define SIDE_STEP 1
#define TILT_MIN (-15)
#define TILT_MAX 15
#define TILT_STEP 1
#define SHOW_SECONDS 2.0
#define SAVE_AFTER 1.0  // seconds with no key, and the ini is written
#define KEEP_FOR 3.0    // seconds after a key when the ini's older value is not read back
#define HIGHWAY_NEAR 1.f
#define HIGHWAY_FAR 1000.f
#define MAX_CAMS 4  // two players may have one each

static int g_zoom = 100, g_height, g_side, g_tilt;
static int g_active;  // in a song with the mod on: read by the hook, game thread as the tick
static int g_dirty;
static double g_changed = -100;
static __thread uint64_t t_cam;

static struct {
  uint64_t cam;
  float base[12], written[12];  // rotation, position
} g_cams[MAX_CAMS];
static int g_next;

static int clamp(long v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : (int)v; }

static int before_set_frustum(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  t_cam = (uint64_t)(uintptr_t)OV_PTR(a, void);
  return 0;
}

static void after_set_frustum(rc_ctx *c, void *user) {
  (void)c, (void)user;
  if (!t_cam) return;
  char *cam = (char *)(uintptr_t)t_cam;
  float fr[6], w[12];
  memcpy(fr, cam + RC_OFF(ni_camera_frustum), sizeof fr);
  if (fr[4] != HIGHWAY_NEAR || fr[5] != HIGHWAY_FAR) return;
  char *world = cam + RC_OFF(ni_world_scale) - sizeof w;  // NiTransform: rotation, translation, scale
  memcpy(w, world, sizeof w);
  if (w[9] == 0 && w[10] == 0 && w[11] == 0) return;

  int i;
  for (i = 0; i < MAX_CAMS; i++)
    if (g_cams[i].cam == t_cam) break;
  if (i == MAX_CAMS) {
    if (!g_active) return;
    i = g_next++ % MAX_CAMS;
    g_cams[i].cam = t_cam;
    g_cams[i].written[11] = w[11] + 1;  // not what is there
  }
  if (memcmp(w, g_cams[i].written, sizeof w)) memcpy(g_cams[i].base, w, sizeof w);
  memcpy(w, g_cams[i].base, sizeof w);
  if (g_active) {
    w[9] += (float)g_side * w[2], w[10] += (float)g_side * w[5], w[11] += (float)g_side * w[8];  // along right
    w[11] += (float)g_height;
    float t = (float)g_tilt * (float)M_PI / 180.f, co = cosf(t), si = sinf(t);
    for (int r = 0; r < 3; r++) {  // direction and up, about right: + looks down
      float d = w[r * 3], u = w[r * 3 + 1];
      w[r * 3] = d * co - u * si;
      w[r * 3 + 1] = u * co + d * si;
    }
  }
  memcpy(g_cams[i].written, w, sizeof w);
  memcpy(world, w, sizeof w);

  if (!g_active || g_zoom == 100) return;
  float k = 100.f / (float)g_zoom;
  for (int s = 0; s < 4; s++) fr[s] *= k;
  memcpy(cam + RC_OFF(ni_camera_frustum), fr, 4 * sizeof *fr);
}

static void save(void) {
  char v[16];
  snprintf(v, sizeof v, "%d", g_zoom);
  rsm_set("Mod Settings", "CameraZoom", v);
  snprintf(v, sizeof v, "%d", g_height);
  rsm_set("Mod Settings", "CameraHeight", v);
  snprintf(v, sizeof v, "%d", g_side);
  rsm_set("Mod Settings", "CameraSide", v);
  snprintf(v, sizeof v, "%d", g_tilt);
  rsm_set("Mod Settings", "CameraTilt", v);
  g_dirty = 0;
}

static void tick(void) {
  int on = rsm_on("CameraDirector");
  double now = rsm_now();
  if (g_dirty && now - g_changed >= SAVE_AFTER) save();
  if (now - g_changed >= KEEP_FOR) {
    g_zoom = clamp(rsm_long("CameraZoom", 100), ZOOM_MIN, ZOOM_MAX);
    g_height = clamp(rsm_long("CameraHeight", 0), HEIGHT_MIN, HEIGHT_MAX);
    g_side = clamp(rsm_long("CameraSide", 0), SIDE_MIN, SIDE_MAX);
    g_tilt = clamp(rsm_long("CameraTilt", 0), TILT_MIN, TILT_MAX);
  }
  g_active = on && rsm_menu_in(RSM_SONG_MODES);
  if (g_active && now - g_changed < SHOW_SECONDS) {
    char txt[96];
    snprintf(txt, sizeof txt, "Camera: zoom %d%%, height %+d, side %+d, tilt %+d", g_zoom, g_height, g_side, g_tilt);
    rsm_text(txt, 0.5f, 1.f / 54, 0.028f, RSM_ALIGN_CENTER, 0xffffffff);
  }
}

static void key(const rsm_key_event *e) {
  if (!e->down || !g_active) return;
  int z = g_zoom, h = g_height, sd = g_side, t = g_tilt;
  if (e->keycode == rsm_key("CameraZoomKey"))
    z = clamp(z + (e->ctrl ? -ZOOM_STEP : ZOOM_STEP), ZOOM_MIN, ZOOM_MAX);
  else if (e->keycode == rsm_key("CameraHeightKey"))
    h = clamp(h + (e->ctrl ? -HEIGHT_STEP : HEIGHT_STEP), HEIGHT_MIN, HEIGHT_MAX);
  else if (e->keycode == rsm_key("CameraSideKey"))
    sd = clamp(sd + (e->ctrl ? -SIDE_STEP : SIDE_STEP), SIDE_MIN, SIDE_MAX);
  else if (e->keycode == rsm_key("CameraTiltKey"))
    t = clamp(t + (e->ctrl ? -TILT_STEP : TILT_STEP), TILT_MIN, TILT_MAX);
  else if (e->keycode == rsm_key("CameraResetKey"))
    z = 100, h = sd = t = 0;
  else
    return;
  g_zoom = z, g_height = h, g_side = sd, g_tilt = t, g_dirty = 1, g_changed = rsm_now();
  rsm_log("camera: zoom %d, height %d, side %d, tilt %d", z, h, sd, t);
}

__attribute__((constructor)) static void init(void) {
  if (!RC_HAS_OFF(ni_camera_frustum) || !RC_HAS_OFF(ni_world_scale)) {
    fprintf(stderr, "[rsmods] the camera's layout isn't known on %s: the camera director stays off\n", rc_target());
    return;
  }
  rsm_hook("camera_set_frustum", before_set_frustum, after_set_frustum);
}

const rsm_mod rsm_mod_camera = {"camera", tick, key, NULL, NULL};
