#!/bin/bash
# Ends RS Tone Host. It also ends by itself a few seconds after the game does.
d=$(cd "$(dirname "$0")" && pwd)
# Only with a host there: with nobody reading the FIFO a write would wait for ever. By the host's own name: a shell or
# an editor whose command line has the app's name in it is no host
pgrep -xq rs-tonehost && echo quit > "$d/commands"
