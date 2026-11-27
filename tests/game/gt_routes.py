"""The routes' building blocks: from the start to the main menu, into a song and out, and back to the main menu
from anywhere. The screens and keys are docs/mac/TESTING.md's and docs/windows/TESTING.md's (Unattended runs, the
screens by key); the waits are the log's, not the clock's.
"""
import re
import time

from gt_game import (DIALOG, HIGHWAY, IN_SONG, MAIN, PAUSE, PROFILES, RIFF, SONG_PAGE, SONGS, TITLE, TUNER,
                     GameEnded, Stuck)
import gt_log as L

# Lines that say a song is loading after Play Song (the tuner takes 15-30 s to come): each one gives the key's wait
# more time. rearmed-mods seven_strings.c:188/255, seven_headstock.c, the steam shim's profile writes.
LOADING = re.compile(r"^\[(rsmods|steam-shim|rs-mods|rc|rs-gl|rs-mtl|songbridge)\] ")
# Play Song taken: the song's tuning stored for the player (rearmed-mods src/seven_strings.c:372, Seven-String on),
# or a guitar profile loaded for it
PLAY_STARTED = re.compile(r"^\[rsmods\] seven-string: strings: target tuning stored \(player 0, song selection\)")


def wait_netguard(d, guard_log, timeout=30):
    """No key before the network guard says it blocks (rs-rearmed tools/perf/run.py)."""
    end = time.time() + timeout
    while time.time() < end:
        if d.find(L.NETGUARD_BLOCK):
            return
        try:
            with open(guard_log) as f:
                if L.NETGUARD_BLOCK.search(f.read()):
                    return
        except OSError:
            pass
        d.poll(0.2)
    raise Stuck("the network guard did not say mode=block in %d s" % timeout, d.screen)


def to_main_menu_from_start(d, autoload, timeout=150):
    """From the start: the title, the profile, the main menu. With ForceProfileLoad on the mod enters the profile
    (mod_autoload.c: Enter twice a second until the main menu): the route only helps if it stays."""
    end = time.time() + timeout
    while d.screen != MAIN:
        if time.time() > end:
            raise Stuck("no main menu %d s after the start" % timeout, d.screen)
        s = d.screen
        idle = time.time() - d.screen_at
        if s == TITLE and (not autoload or idle > 20):
            # a Return in the title's fade-in is lost: give it a few seconds, then retry
            d.press("return", (PROFILES, DIALOG, MAIN), settle=3.0, retry=4, tries=6)
        elif s == PROFILES and (not autoload or idle > 15):
            d.press("return", (DIALOG, MAIN, PROFILES), settle=1.5, retry=4, tries=4)
        elif s == "MissionMenu":
            d.press("return", lambda: d.screen != "MissionMenu", settle=1.0, retry=3, tries=5)
        elif s == DIALOG and (not autoload or idle > 15):
            d.press("return", lambda: d.screen != DIALOG, settle=1.0, retry=4, tries=4)
        else:
            d.poll(0.2)
    return d.screen


def wait_dlc(d, timeout=90):
    """The end of the DLC mounting (mods_shared.c:544): the main menu takes keys after it."""
    return d.wait_for(L.DLC_MOUNTED, timeout, mark=0, what="the DLC mounting's line")


def main_menu_ready(d):
    """On the main menu, and the menu has stopped dropping keys (after the DLC line)."""
    d.poll(0)
    if d.screen != MAIN:
        home(d)
    wait_dlc(d)


# ---------------------------------------------------------------------------------------------------------------
# Learn a Song

def main_menu_top(d):
    """The main menu keeps its cursor (a case that went to the Guitarcade leaves it there): Up to LEARN A SONG."""
    for _ in range(9):
        d.key("up")
        d.poll(0.15)


def open_song_list(d):
    main_menu_ready(d)
    main_menu_top(d)
    d.press("return", SONGS, settle=1.0, retry=6, tries=4, stray=True)


def find_song(d, key, search, tries=2):
    """On the song list: the search, its letters, then Down until the highlighted song (`song:` lines) is key."""
    for attempt in range(tries):
        d.poll(1.0 if attempt == 0 else 0.5)
        mark = d.mark()
        d.key("search")
        d.poll(0.6)
        d.text(search, gap=1.0 + 0.5 * attempt)
        # the first match is highlighted a moment after the last letter
        d.hold(2.0, until=lambda: d.find(L.SONG, mark) is not None and d.song == key)
        if d.song == key:
            return True
        for _ in range(25):
            prev_mark = d.mark()
            d.key("down")
            d.hold(1.5, until=lambda: d.find(L.SONG, prev_mark) is not None)
            if d.song == key:
                return True
            if d.find(L.SONG, prev_mark) is None:
                break  # the end of the matches
        d.note("%s not among the matches of \"%s\": again" % (key, search))
        # Esc clears the search (with no match it closes it); the screen stays the list
        d.key("esc")
        d.poll(0.8)
        if d.screen != SONGS:
            open_song_list(d)
    raise Stuck("%s is not in the song list (search \"%s\")" % (key, search), d.screen)


def open_song(d, key, search):
    """From anywhere to the song's page."""
    if d.screen not in (SONGS,):
        open_song_list(d)
    find_song(d, key, search)
    d.press("return", SONG_PAGE, settle=0.5, retry=5, tries=3)


def play_from_page(d, tuner=None):
    """On the song's page: Play Song, the tuner, the highway. tuner(d): what to do on the tuner before skipping it
    (checks, a snapshot). The tuner comes 15-30 s after Play Song; a profile with no input has "TUNE UP!" instead."""
    d.press("return", (TUNER, DIALOG), settle=2.5, retry=8, tries=3, started=PLAY_STARTED, started_wait=90)
    if d.screen == TUNER and tuner:
        tuner(d)
    pass_tuner(d)


def pass_tuner(d):
    """Skip Tuner (forward delete), Return on "Yes, continue without tuning" (or on "TUNE UP!"), the highway."""
    for _ in range(3):
        if d.screen == HIGHWAY:
            return
        if d.screen == TUNER:
            d.press("fwddelete", lambda: d.screen != TUNER or d.find(r"^\[rsmods\] menu: " + DIALOG, d.screen_index),
                    settle=3.0, retry=6, tries=3)
        if d.screen == DIALOG:
            d.press("return", lambda: d.screen != DIALOG, settle=1.0, retry=9, tries=3)
        try:
            d.wait_screen(HIGHWAY, 60, what="the note highway after the tuner")
            return
        except Stuck:
            if d.screen not in (TUNER, DIALOG):
                raise
    raise Stuck("the tuner was not passed", d.screen)


def page_bottom(d):
    """The song's page: Play Song at the top (Up does nothing there), then Rocksmith Recommends (up to three),
    OTHER ARRANGEMENTS and Score Attack last; Down does not wrap, so twelve Downs reach Score Attack whatever the
    page holds."""
    d.poll(1.0)
    for _ in range(12):
        d.key("down")
        d.poll(0.15)


def pick_bass(d):
    """The page's last other arrangement, the Bass (Lead, Rhythm, Bass are listed in that order): one Up from Score
    Attack. Its Return plays it (the bass's tuner)."""
    page_bottom(d)
    d.key("up")
    d.poll(0.3)


def learn_song(d, key, search, tuner=None, arrangement=None):
    open_song(d, key, search)
    if arrangement:
        arrangement(d)
    play_from_page(d, tuner)


# ---------------------------------------------------------------------------------------------------------------
# In a song

def pause(d):
    """Esc on the highway: the pause menu."""
    if d.screen == PAUSE:
        return
    if d.screen != HIGHWAY:
        raise Stuck("not in a song (on %s): no pause menu" % d.screen, d.screen)
    d.press("esc", PAUSE, settle=1.5, retry=3, tries=4)


def pause_menu_top(d):
    """The pause menu keeps its cursor: Up six times puts it on RESUME (docs/mac/TESTING.md, In a song)."""
    for _ in range(6):
        d.key("up")
        d.poll(0.25)


def _left_song(d, seconds):
    """After EXIT: the song's page comes a few seconds later, the highway's screen name briefly before it."""
    d.hold(seconds, until=lambda: d.screen == SONG_PAGE)
    return d.screen == SONG_PAGE


def exit_song(d):
    """From the song (playing or paused): the pause menu's EXIT, the last entry (Down seven times from anywhere
    reaches it whatever is greyed), to the song's page."""
    if d.screen == RIFF:
        d.press("fwddelete", lambda: d.screen != RIFF, settle=1.0, retry=3, tries=3)
    for attempt in range(2):
        if d.screen == SONG_PAGE:
            return
        pause(d)
        d.poll(0.5)
        for _ in range(7):
            d.key("down")
            d.poll(0.2)
        d.key("return")
        if _left_song(d, 12):
            return
        if d.screen == HIGHWAY:  # RESUME or RESTART: the cursor wraps? Up once from the top is the last
            d.note("EXIT not reached by Down: from the top, Up once")
            pause(d)
            d.poll(0.5)
            pause_menu_top(d)
            d.key("up")
            d.poll(0.3)
            d.key("return")
            if _left_song(d, 12):
                return
    raise Stuck("the song was not left by its pause menu", d.screen)


# ---------------------------------------------------------------------------------------------------------------
# Back to the main menu from anywhere

def home(d, timeout=90):
    """Esc steps to the main menu; a song by its pause menu's EXIT. Esc on the main menu asks to quit, so the loop
    stops there; that question (a dialog right after the main menu) is answered No (Down, Return), and a Yes that
    went through leads to the title, from where the profile is entered again."""
    end = time.time() + timeout
    escs = 0
    while True:
        if d.screen == MAIN:
            return
        if time.time() > end:
            raise Stuck("not back to the main menu in %d s" % timeout, d.screen)
        if d.screen in IN_SONG:
            exit_song(d)
            continue
        if d.screen == TITLE:
            to_main_menu_from_start(d, autoload=False, timeout=60)
            continue
        if d.screen == PROFILES:
            d.press("return", lambda: d.screen != PROFILES, settle=1.0, retry=4, tries=3)
            continue
        prev = d.history[-2][1] if len(d.history) > 1 else None
        if d.screen == DIALOG and prev == MAIN:
            d.note("the quit question over the main menu: No")
            d.key("down")
            d.poll(0.3)
            before = d.screen_at
            d.key("return")
            d.hold(3, until=lambda: d.screen_at != before)
            continue
        before = (d.screen, d.screen_at)
        d.key("esc")
        d.hold(4, until=lambda: (d.screen, d.screen_at) != before)
        if (d.screen, d.screen_at) != before:
            escs = 0
            continue
        escs += 1
        if escs >= 3:  # three Escs on one screen and no change: a dialog that wants its Return
            d.note("Esc does nothing on %s: Return" % d.screen)
            d.key("return")
            d.hold(4, until=lambda: (d.screen, d.screen_at) != before)
            escs = 0


def recover(d, timeout=90):
    """After a stuck route: home(), or False when that fails too (the game is then restarted)."""
    try:
        home(d, timeout)
        return True
    except GameEnded:
        return False
    except Stuck as e:
        d.note("recover: %s" % e)
        return False


# ---------------------------------------------------------------------------------------------------------------
# Leaving the game by its own menus (a normal quit, which saves the profile)

def exit_game(d):
    """Esc on the main menu, Return on "Are you sure you want to quit?" (the title), Esc there and Return on EXIT
    GAME? (docs/mac/TESTING.md, Quitting). -> True when the game ended."""
    main_menu_ready(d)
    try:
        d.press("esc", DIALOG, settle=1.5, retry=3, tries=3)
        d.press("return", TITLE, settle=0.8, retry=4, tries=3)
        d.press("esc", DIALOG, settle=3.0, retry=5, tries=4)
        # EXIT GAME?: a Return while the dialog moves in is lost
        for attempt in range(3):
            d.hold(1.5)
            mark = d.mark()
            d.key("return")
            end = time.time() + 12
            while time.time() < end:
                d.poll(0.2)
                if d.find(L.MENU, mark):
                    break
            if d.screen == TITLE and d.find(L.MENU, mark):
                d.note("back on the title after EXIT GAME?: Esc again")
                d.press("esc", DIALOG, settle=2.0, retry=5, tries=3)
            else:
                d.note("still on EXIT GAME?: Return again")
    except GameEnded:
        return True
    raise Stuck("the game did not quit by EXIT GAME", d.screen)
