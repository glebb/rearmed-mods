// RSMods: your own plug-in in place of the game's tones (ToneHost), with rs-tonehost from RS Re-ARMed playing the guitar.
// On a tone change the game sets every knob of the gear as a Wwise RTPC by name (Amp_GB38_Gain,
// Rack_StudioCompressor_Ratio), then the tone's balance and P1_NoiseFloor, all in one go, about two seconds before the
// change in the songs tried. This collects them and writes one "gear ..." line to the host's FIFO. While the host is
// there the game's own guitar is silent (Mixer_Player1 at 0: notes still score), and the guitar fader goes to the
// host as "mixer VALUE". With no host, or ToneHost off, the game plays its own tones. The empty line each second shows a
// host gone away as a failed write, and a host that starts late gets the tone in play. The game starts the host if it
// is installed (tonehost/start.sh); the host ends by itself when those lines stop, so also after a crash.
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "rsmods.h"

#define FADER "Mixer_Player1"

static char g_line[8192], g_last[8192], g_path[900];
static size_t g_len, g_last_len;
static int g_fd = -1, g_frames, g_was_on;
static int g_self, g_remute;  // our own call is under way; the game has just set the fader
static float g_fader = -1;    // what the game or a key asked for
static char g_start[900];     // the host's start script
static int g_starts;
static double g_started_at;

extern char **environ;

// A host that doesn't come is tried again three times, not every second
static void start_host(void) {
  if (g_starts >= 3 || (g_starts && rsm_now() - g_started_at < 30) || access(g_start, X_OK)) return;
  g_starts++;
  g_started_at = rsm_now();
  char *argv[] = {"/bin/bash", g_start, NULL};
  pid_t pid;
  int e = posix_spawn(&pid, argv[0], NULL, NULL, argv, environ);
  rsm_log("tone host: starting it (%s)", e ? "failed" : "started");
}

static int is_gear(const char *n) {
  static const char *const prefixes[] = {"Amp_", "Bass_", "DI_", "Pedal_", "Rack_", "Meter_Tone_Balance_Return", NULL};
  for (int i = 0; prefixes[i]; i++)
    if (!strncmp(n, prefixes[i], strlen(prefixes[i]))) return 1;
  return 0;
}

static void send_text(const char *text, size_t n) {
  if (g_fd < 0) return;
  if (write(g_fd, text, n) < 0 && errno != EAGAIN) {
    close(g_fd);
    g_fd = -1;
  }
}

static void send_fader(void) {
  char text[32];
  send_text(text, (size_t)snprintf(text, sizeof text, "mixer %g\n", g_fader));
}

static int before_rtpc(rc_ctx *c, void *user) {
  (void)user;
  OV_ARGS(a, c);
  const char *name = OV_PTR(a, const char);
  float value = OV_F32(a);
  if (!name) return 0;
  if (!strcmp(name, FADER)) {
    if (!g_self) {
      g_fader = value;
      send_fader();
      g_remute = 1;
    }
  } else if (!strcmp(name, "P1_NoiseFloor")) {  // the last of a tone's values
    if (g_len) {
      if (g_len < sizeof g_line - 1) g_line[g_len++] = '\n';
      send_text(g_line, g_len);
      memcpy(g_last, g_line, g_len);
      g_last_len = g_len;
    }
    g_len = 0;
  } else if (is_gear(name)) {
    if (!g_len) g_len = (size_t)snprintf(g_line, sizeof g_line, "gear");
    if (g_len < sizeof g_line - 64) g_len += (size_t)snprintf(g_line + g_len, sizeof g_line - g_len, " %s=%g", name, value);
  }
  return 0;
}

static void set_fader(float v) {
  g_self = 1;
  ak_set_rtpc(FADER, v, AK_INVALID_GAME_OBJECT, 0, AK_CURVE_LINEAR);
  ak_set_rtpc(FADER, v, RS_SONG_GAME_OBJECT, 0, AK_CURVE_LINEAR);
  g_self = 0;
}

static void give_back(void) {
  if (g_was_on) set_fader(g_fader >= 0 ? g_fader : 100.f);
  g_was_on = 0;
}

int rsm_guitar_fader(float *value) {
  if (g_fd < 0 || g_fader < 0) return 0;
  *value = g_fader;
  return 1;
}

static void tick(void) {
  if (!rsm_on("ToneHost")) {
    if (g_fd >= 0) {
      close(g_fd);
      g_fd = -1;
    }
    give_back();
    return;
  }
  if (g_remute && g_fd >= 0) {
    g_remute = 0;
    set_fader(0.f);
  }
  if (++g_frames % 60) return;
  send_text("\n", 1);
  if (g_fd < 0) {
    g_fd = open(g_path, O_WRONLY | O_NONBLOCK);  // fails with no reader: no host, no change
    if (g_fd >= 0) {
      rsm_log("tone host found");
      if (g_last_len) send_text(g_last, g_last_len);
      if (g_fader >= 0) send_fader();
    } else
      start_host();
  }
  if (g_fd >= 0) {
    set_fader(0.f);
    g_was_on = 1;
  } else
    give_back();
}

__attribute__((constructor)) static void init(void) {
  const char *p = getenv("RS_TONEHOST_FIFO"), *h = getenv("HOME");
  if (p) snprintf(g_path, sizeof g_path, "%s", p);
  else snprintf(g_path, sizeof g_path, "%s/Library/Application Support/RS Re-ARMed/tonehost/commands", h ? h : "");
  size_t n = strlen(g_path);
  while (n && g_path[n - 1] != '/') n--;
  snprintf(g_start, sizeof g_start, "%.*sstart.sh", (int)n, g_path);
  signal(SIGPIPE, SIG_IGN);
  rsm_hook("ak_set_rtpc_name", before_rtpc, NULL);
}

const rsm_mod rsm_mod_tone_host = {"tone_host", tick, NULL, NULL, NULL};
