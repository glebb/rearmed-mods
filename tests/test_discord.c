// Discord Rich Presence without Discord (src/discord_ipc.c): the activity's JSON, a message's bytes, and a
// conversation with a socket that answers as Discord's desktop app does: the handshake, an activity, a ping, an
// error, and the end.
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "../src/discord_ipc.h"
#include "check.h"

static int has(const char *json, const char *part) { return strstr(json, part) != NULL; }

// --- a Discord: one connection at discord-ipc-3, what it was sent, and what it answers ---
static int g_listen;
static char g_got[8][2048];
static uint32_t g_got_op[8];
static _Atomic int g_ngot;
static int g_refuse;  // answer the handshake with a close

static int read_n(int fd, void *p, size_t n) {
  for (size_t got = 0; got < n;) {
    ssize_t r = read(fd, (char *)p + got, n - got);
    if (r <= 0) return 0;
    got += (size_t)r;
  }
  return 1;
}
static void reply(int fd, uint32_t op, const char *json) {
  uint8_t m[2048];
  size_t n = discord_frame(m, sizeof m, op, json);
  write(fd, m, n);
}
static void *discord(void *arg) {
  int fd = accept(g_listen, NULL, NULL);
  for (;;) {
    uint32_t h[2];
    if (!read_n(fd, h, 8) || h[1] >= sizeof g_got[0] || g_ngot >= 8) break;
    int i = g_ngot;
    g_got_op[i] = h[0];
    if (!read_n(fd, g_got[i], h[1])) break;
    g_got[i][h[1]] = 0;
    g_ngot++;
    if (h[0] == 0 && g_refuse) {
      reply(fd, 2, "{\"code\":4000,\"message\":\"Invalid Client ID\"}");
      break;
    }
    if (h[0] == 0) reply(fd, 1, "{\"cmd\":\"DISPATCH\",\"data\":{\"v\":1},\"evt\":\"READY\",\"nonce\":null}");
    if (h[0] == 1 && has(g_got[i], "\"nonce\":\"1\"")) {
      reply(fd, 1, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"evt\":null,\"nonce\":\"1\"}");
      reply(fd, 3, "{\"ping\":42}");
    }
    if (h[0] == 1 && has(g_got[i], "\"nonce\":\"2\""))
      reply(fd, 1, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{\"code\":4000,\"message\":\"bad\"},\"evt\":\"ERROR\",\"nonce\":\"2\"}");
    if (h[0] == 1 && has(g_got[i], "\"nonce\":\"3\"")) break;  // Discord quits
  }
  close(fd);
  return NULL;
}
static void wait_for(int n) {
  for (int i = 0; i < 400 && g_ngot < n; i++) usleep(5000);
}

int main(void) {
  // --- the activity ---
  discord_activity a = {.details = "Ace of \"Spades\"", .state = "Mot\xc3\xb6rhead \xc2\xb7 Learn a Song",
                        .image = "logo", .image_text = "RS Re-ARMed", .start = 1700000000};
  char json[2048];
  size_t n = discord_activity_json(json, sizeof json, &a, 4321, 7);
  CHECK(n == strlen(json) && has(json, "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":4321,\"activity\":{\"type\":0,"));
  CHECK(has(json, "\"details\":\"Ace of \\\"Spades\\\"\",\"state\":\"Mot\xc3\xb6rhead \xc2\xb7 Learn a Song\""));
  CHECK(has(json, "\"timestamps\":{\"start\":1700000000}"));
  CHECK(has(json, "\"assets\":{\"large_image\":\"logo\",\"large_text\":\"RS Re-ARMed\"}}},\"nonce\":\"7\"}"));
  // what isn't set isn't sent
  discord_activity menus = {.details = "In the menus"};
  CHECK(discord_activity_json(json, sizeof json, &menus, 1, 1));
  CHECK(!has(json, "state") && !has(json, "timestamps") && !has(json, "assets") && has(json, "\"In the menus\""));
  // a one-character line gets a second character, control characters and backslashes are escaped
  discord_activity odd = {.details = "X", .state = "a\\b\n\tc"};
  CHECK(discord_activity_json(json, sizeof json, &odd, 1, 1));
  CHECK(has(json, "\"details\":\"X\xc2\xa0\"") && has(json, "\"state\":\"a\\\\b\\u000a\\u0009c\""));
  // a line of more than 128 characters is cut to 128, counted in characters
  discord_activity longer = {0};
  for (int i = 0; i < 70; i++) strcat(longer.state, "\xc3\xa9z");  // 140 characters, 210 bytes
  CHECK(discord_activity_json(json, sizeof json, &longer, 1, 1));
  const char *st = strstr(json, "\"state\":\"") + 9, *end = strstr(st, "\"");
  int chars = 0;
  for (const char *c = st; c < end; c++) chars += ((unsigned char)*c & 0xc0) != 0x80;
  CHECK(chars == 128 && !strncmp(end - 3, "...", 3) && !strncmp(st, longer.state, 187));
  memset(longer.state, 0, sizeof longer.state);
  for (int i = 0; i < 128; i++) strcat(longer.state, "y");  // exactly 128: as it is
  CHECK(discord_activity_json(json, sizeof json, &longer, 1, 1) && !has(json, "..."));
  // a buffer too small gives nothing
  CHECK(!discord_activity_json(json, 60, &a, 1, 1) && !discord_activity_json(json, 0, &a, 1, 1));

  // --- a message ---
  uint8_t m[32];
  CHECK(discord_frame(m, sizeof m, 3, "{\"a\":1}") == 15 && !memcmp(m, "\3\0\0\0\7\0\0\0{\"a\":1}", 15));
  CHECK(!discord_frame(m, 14, 3, "{\"a\":1}"));

  // --- the conversation ---
  char dir[] = "/tmp/rsmods-discord-XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  CHECK(discord_connect(dir, "123") == -1);  // no Discord running
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  snprintf(addr.sun_path, sizeof addr.sun_path, "%s/discord-ipc-3", dir);
  g_listen = socket(AF_UNIX, SOCK_STREAM, 0);
  CHECK(!bind(g_listen, (struct sockaddr *)&addr, sizeof addr) && !listen(g_listen, 2));
  CHECK(discord_connect(dir, "12a") == -1 && discord_connect(dir, "") == -1);  // an ID is digits
  pthread_t t;
  pthread_create(&t, NULL, discord, NULL);
  int fd = discord_connect(dir, "1570119228806267021");
  CHECK(fd >= 0 && g_ngot == 1 && g_got_op[0] == 0);
  CHECK(!strcmp(g_got[0], "{\"v\":1,\"client_id\":\"1570119228806267021\"}"));
  char error[256] = "x";
  CHECK(discord_poll(fd, error, sizeof error) && !*error);  // nothing waiting
  CHECK(discord_set_activity(fd, &a, 1));
  wait_for(2);
  CHECK(g_ngot == 2 && g_got_op[1] == 1 && has(g_got[1], "Ace of") && has(g_got[1], "\"nonce\":\"1\""));
  usleep(50000);  // Discord's answer and its ping are there: the poll answers the ping
  CHECK(discord_poll(fd, error, sizeof error) && !*error);
  wait_for(3);
  CHECK(g_got_op[2] == 4 && !strcmp(g_got[2], "{\"ping\":42}"));
  CHECK(discord_set_activity(fd, &menus, 2));
  wait_for(4);
  usleep(50000);
  CHECK(discord_poll(fd, error, sizeof error) && has(error, "\"evt\":\"ERROR\""));  // told, and still connected
  CHECK(discord_set_activity(fd, &menus, 3));
  pthread_join(t, NULL);
  CHECK(!discord_poll(fd, error, sizeof error));  // Discord has quit
  discord_set_activity(fd, &menus, 4);            // no signal, whatever it returns
  CHECK(!discord_set_activity(fd, &menus, 5));
  discord_close(fd);
  discord_close(-1);

  // a Discord that refuses the application
  g_ngot = 0, g_refuse = 1;
  pthread_create(&t, NULL, discord, NULL);
  CHECK(discord_connect(dir, "42") == -1);
  pthread_join(t, NULL);
  close(g_listen);

  char cmd[200];
  snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
  CHECK(!system(cmd));
  return check_done("discord");
}
