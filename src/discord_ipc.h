// Discord Rich Presence, the part without the game (tests/test_discord.c): the activity as Discord's desktop app takes
// it, and the connection to it. Discord listens on a Unix socket in the user's temporary folder, discord-ipc-0 to
// discord-ipc-9. A message is two little-endian 32-bit words, an opcode (0 handshake, 1 frame, 2 close, 3 ping,
// 4 pong) and a length, then JSON. The handshake names a Discord application by its ID: the application's name is
// what Discord shows after "Playing". SET_ACTIVITY then sets what is under it, for this process; the activity goes
// when the connection closes. One thread at a time.
#ifndef DISCORD_IPC_H
#define DISCORD_IPC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char details[160], state[320];  // the first and second line under the name; "" = none
  char image[40], image_text[80]; // the application's art asset by name, and its tooltip; "" = none
  int64_t start;                  // Unix seconds the elapsed time counts from; 0 = none
} discord_activity;

// The SET_ACTIVITY command's JSON. A line is at most 128 characters for Discord and at least 2: a longer one is cut
// (with "..."), a one-character one gets a no-break space. Its length, or 0 when it doesn't fit.
size_t discord_activity_json(char *out, size_t n, const discord_activity *a, int pid, unsigned nonce);
// A message (header and JSON) in out; its length, or 0 when it doesn't fit
size_t discord_frame(uint8_t *out, size_t n, uint32_t op, const char *json);

// Connects to the running Discord and shakes hands as the application: a socket, or -1 (no Discord running, or it
// refused). `dir`: the folder with the socket, NULL = the user's temporary folder.
int discord_connect(const char *dir, const char *application_id);
// Sends the activity: 1, or 0 when the connection is gone
int discord_set_activity(int fd, const discord_activity *a, unsigned nonce);
// Reads what Discord has sent since, without waiting, and answers its pings: 1, or 0 when the connection is gone.
// `error` (may be NULL, n bytes): the last message that reported an error, "" if none.
int discord_poll(int fd, char *error, size_t n);
void discord_close(int fd);

#endif
