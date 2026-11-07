// RSMods: text over the game (RSMods' D3DOverlay). Mods queue text during the frame hook (rsm_text). Each
// string is rendered once with Core Text into an RGBA bitmap (cached while it is drawn) and drawn after the
// game's frame, before the shell shows it, in one of two ways:
//  - RS Re-ARMed 1.3.0 and later export the overlay API (rs_overlay.h, found with dlsym at the first frame): a
//    callback the shell runs before it shows the frame draws the bitmaps as overlay images. No GL here; it
//    works on Metal (their default renderer) and on OpenGL.
//  - 1.2.0, OpenGL only: rsm_overlay_draw, right after the mods' tick while the game's OpenGL context is still
//    current (rsp_shell.m drawView), draws them as textured quads with the fixed-function pipeline, the
//    game's GL state saved and restored around it.
#define GL_SILENCE_DEPRECATION
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rsmods.h"
#include "rs_overlay.h"

#define MAX_ITEMS 32
#define MAX_CACHE 48
#define CACHE_IDLE_FRAMES 120  // a texture not drawn for this many frames is freed

typedef struct {
  char text[128];
  float x, y, size;  // x, y: fractions of the window, from the top left; size: fraction of its height
  int align;
  uint32_t rgba;
} item;
static item g_items[MAX_ITEMS];
static int g_nitems;

typedef struct {
  char text[128];
  int px;  // font size in pixels
  uint32_t rgba;
  GLuint tex;             // old builds
  rs_overlay_image *img;  // overlay API
  int w, h;
  unsigned last;
} cached;
static cached g_cache[MAX_CACHE];
static unsigned g_frame;

// The overlay API of the executable, if it has one (resolved at the first frame).
static struct {
  int (*reg)(rs_overlay_fn, void *);
  rs_overlay_image *(*create)(rs_overlay_frame *, int, int);
  int (*update)(rs_overlay_frame *, rs_overlay_image *, const void *, int);
  void (*destroy)(rs_overlay_frame *, rs_overlay_image *);
  int (*draw)(rs_overlay_frame *, rs_overlay_image *, float, float, float, float, float);
  int (*read)(rs_overlay_frame *, int, int, int, int, void *);
} g_api;
static int g_use_api = -1;      // -1 not looked up yet, 0 old path, 1 overlay API
static rs_overlay_frame *g_of;  // inside the overlay callback

static char g_shot_path[1024];  // a screenshot to save after this frame's drawing

void rsm_screenshot(const char *path) { snprintf(g_shot_path, sizeof g_shot_path, "%s", path); }

typedef struct {
  char path[1024];
  int w, h;
  int bottom_up;  // rows bottom first (glReadPixels), else top first (rs_overlay_read_pixels)
  uint8_t *px;    // RGBA
} shot;

// PNG encoding off the game thread
static void *save_shot(void *arg) {
  shot *s = arg;
  size_t row = (size_t)s->w * 4;
  uint8_t *tmp = malloc(row);
  for (int y = 0; tmp && s->bottom_up && y < s->h / 2; y++) {  // top row first
    memcpy(tmp, s->px + y * row, row);
    memcpy(s->px + y * row, s->px + (s->h - 1 - y) * row, row);
    memcpy(s->px + (s->h - 1 - y) * row, tmp, row);
  }
  free(tmp);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef g = CGBitmapContextCreate(s->px, s->w, s->h, 8, row, cs, kCGImageAlphaNoneSkipLast);
  CGImageRef im = g ? CGBitmapContextCreateImage(g) : NULL;
  CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)s->path, (CFIndex)strlen(s->path), false);
  CGImageDestinationRef d = url && im ? CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL) : NULL;
  int ok = 0;
  if (d) {
    CGImageDestinationAddImage(d, im, NULL);
    ok = CGImageDestinationFinalize(d);
    CFRelease(d);
  }
  rsm_log("screenshot %s: %s", ok ? "saved" : "FAILED", s->path);
  if (url) CFRelease(url);
  if (im) CGImageRelease(im);
  if (g) CGContextRelease(g);
  CGColorSpaceRelease(cs);
  free(s->px);
  free(s);
  return NULL;
}

// Reads the window's back buffer (the frame about to be shown, with the overlay) for rsm_screenshot.
static void take_screenshot(int W, int H) {
  shot *s = calloc(1, sizeof *s);
  if (!s || !(s->px = malloc((size_t)W * H * 4))) {
    free(s);
    return;
  }
  snprintf(s->path, sizeof s->path, "%s", g_shot_path);
  s->w = W;
  s->h = H;
  if (g_of) {
    if (g_api.read(g_of, 0, 0, W, H, s->px)) {
      free(s->px);
      free(s);
      return;
    }
  } else {
    s->bottom_up = 1;
    GLint fb, read_buf, pack_buf;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &fb);
    glGetIntegerv(GL_READ_BUFFER, &read_buf);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buf);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, s->px);
    glPopClientAttrib();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack_buf);
    glReadBuffer((GLenum)read_buf);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)fb);
  }
  pthread_t t;
  if (pthread_create(&t, NULL, save_shot, s) == 0)
    pthread_detach(t);
  else {
    free(s->px);
    free(s);
  }
}

// Only the last tick's text shows: Learn & Play can tick more than once between two frames
void rsm_overlay_begin(void) { g_nitems = 0; }

void rsm_text(const char *text, float x, float y, float size, int align, uint32_t rgba) {
  if (g_nitems == MAX_ITEMS || !text || !*text) return;
  item *it = &g_items[g_nitems++];
  snprintf(it->text, sizeof it->text, "%s", text);
  it->x = x;
  it->y = y;
  it->size = size;
  it->align = align;
  it->rgba = rgba;
}

// The window's size in pixels: [[NSOpenGLContext currentContext] view], its bounds in backing pixels.
static int window_size(int *w, int *h) {
  Class ctxc = objc_getClass("NSOpenGLContext");
  if (!ctxc) return 0;
  id ctx = ((id(*)(Class, SEL))objc_msgSend)(ctxc, sel_registerName("currentContext"));
  if (!ctx) return 0;
  id view = ((id(*)(id, SEL))objc_msgSend)(ctx, sel_registerName("view"));
  if (!view) return 0;
  CGRect b = ((CGRect(*)(id, SEL))objc_msgSend)(view, sel_registerName("bounds"));
  b = ((CGRect(*)(id, SEL, CGRect))objc_msgSend)(view, sel_registerName("convertRectToBacking:"), b);
  *w = (int)b.size.width;
  *h = (int)b.size.height;
  return *w > 0 && *h > 0;
}

// Renders text into a new texture: white-on-shadow style, in the item's colour.
static int render(cached *e) {
  CFStringRef str = CFStringCreateWithCString(NULL, e->text, kCFStringEncodingUTF8);
  if (!str) return 0;
  CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica-Bold"), e->px, NULL);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGFloat comp[4] = {(e->rgba >> 24 & 0xff) / 255.0, (e->rgba >> 16 & 0xff) / 255.0, (e->rgba >> 8 & 0xff) / 255.0,
                     (e->rgba & 0xff) / 255.0};
  CGColorRef color = CGColorCreate(cs, comp);
  CFStringRef keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
  CFTypeRef vals[] = {font, color};
  CFDictionaryRef attrs = CFDictionaryCreate(NULL, (const void **)keys, (const void **)vals, 2,
                                             &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFAttributedStringRef as = CFAttributedStringCreate(NULL, str, attrs);
  CTLineRef line = CTLineCreateWithAttributedString(as);
  CGFloat ascent, descent, leading;
  double tw = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
  int pad = e->px / 6 + 2;
  int w = (int)ceil(tw) + 2 * pad, h = (int)ceil(ascent + descent) + 2 * pad;
  void *px = calloc((size_t)w * h, 4);
  int ok = 0;
  if (px) {
    CGContextRef g = CGBitmapContextCreate(px, w, h, 8, (size_t)w * 4, cs, kCGImageAlphaPremultipliedLast);
    if (g) {
      CGFloat shadow[4] = {0, 0, 0, 0.9};
      CGColorRef sc = CGColorCreate(cs, shadow);
      CGContextSetShadowWithColor(g, CGSizeMake(0, 0), pad * 0.8, sc);
      for (int pass = 0; pass < 2; pass++) {  // twice: a darker shadow
        CGContextSetTextPosition(g, pad, pad + descent);  // CTLineDraw advances it
        CTLineDraw(line, g);
      }
      CGContextFlush(g);
      CGColorRelease(sc);
      CGContextRelease(g);
      if (g_of) {
        e->img = g_api.create(g_of, w, h);
        ok = e->img && g_api.update(g_of, e->img, px, w * 4) == 0;
      } else {
        glGenTextures(1, &e->tex);
        glBindTexture(GL_TEXTURE_2D, e->tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        ok = 1;
      }
      e->w = w;
      e->h = h;
    }
    free(px);
  }
  CFRelease(line);
  CFRelease(as);
  CFRelease(attrs);
  CGColorRelease(color);
  CGColorSpaceRelease(cs);
  CFRelease(font);
  CFRelease(str);
  return ok;
}

static int live(const cached *e) { return e->tex || e->img; }

static void release(cached *e) {
  if (e->img) g_api.destroy(g_of, e->img);
  if (e->tex) glDeleteTextures(1, &e->tex);
  memset(e, 0, sizeof *e);
}

static cached *texture_for(const item *it, int px) {
  cached *free_slot = NULL, *oldest = &g_cache[0];
  for (int i = 0; i < MAX_CACHE; i++) {
    cached *e = &g_cache[i];
    if (live(e) && e->px == px && e->rgba == it->rgba && !strcmp(e->text, it->text)) return e;
    if (!live(e) && !free_slot) free_slot = e;
    if (e->last < oldest->last) oldest = e;
  }
  cached *e = free_slot ? free_slot : oldest;
  release(e);
  snprintf(e->text, sizeof e->text, "%s", it->text);
  e->px = px;
  e->rgba = it->rgba;
  if (!render(e)) {
    release(e);
    return NULL;
  }
  return e;
}

// Takes this frame's items and frees the textures nothing drew lately; returns the number of items.
static int frame_start(void) {
  g_frame++;
  int n = g_nitems;
  g_nitems = 0;
  for (int i = 0; i < MAX_CACHE; i++)
    if (live(&g_cache[i]) && g_frame - g_cache[i].last > CACHE_IDLE_FRAMES) release(&g_cache[i]);
  return n;
}

// The item's top-left corner in pixels, its texture made if needed.
static cached *place(const item *it, int W, int H, float *x, float *y) {
  int px = (int)(it->size * H + 0.5f);
  if (px < 8) px = 8;
  cached *e = texture_for(it, px);
  if (!e) return NULL;
  e->last = g_frame;
  *x = it->x * W;
  *y = it->y * H;
  if (it->align == RSM_ALIGN_CENTER) *x -= e->w / 2.f;
  else if (it->align == RSM_ALIGN_RIGHT) *x -= e->w;
  return e;
}

// --- overlay API ---

// The shell's overlay callback: after the game's frame (and so after the mods' tick that queued the items).
static void overlay_cb(rs_overlay_frame *f, void *user) {
  (void)user;
  g_of = f;
  int n = frame_start();
  for (int i = 0; i < n; i++) {
    float x, y;
    cached *e = place(&g_items[i], f->width, f->height, &x, &y);
    if (e) g_api.draw(f, e->img, x, y, (float)e->w, (float)e->h, 1.0f);
  }
  if (*g_shot_path) {
    take_screenshot(f->width, f->height);
    *g_shot_path = 0;
  }
  g_of = NULL;
}

static int use_api(void) {
  if (g_use_api >= 0) return g_use_api;
  g_use_api = 0;
  int (*ver)(void) = (int (*)(void))dlsym(RTLD_DEFAULT, "rs_overlay_api_version");
  if (!ver || ver() < 1) {
    rsm_log("overlay: OpenGL (no overlay API in this RS Re-ARMed build)");
    return 0;
  }
  *(void **)&g_api.reg = dlsym(RTLD_DEFAULT, "rs_overlay_register");
  *(void **)&g_api.create = dlsym(RTLD_DEFAULT, "rs_overlay_image_create");
  *(void **)&g_api.update = dlsym(RTLD_DEFAULT, "rs_overlay_image_update");
  *(void **)&g_api.destroy = dlsym(RTLD_DEFAULT, "rs_overlay_image_destroy");
  *(void **)&g_api.draw = dlsym(RTLD_DEFAULT, "rs_overlay_draw_image");
  *(void **)&g_api.read = dlsym(RTLD_DEFAULT, "rs_overlay_read_pixels");
  if (g_api.reg && g_api.create && g_api.update && g_api.destroy && g_api.draw && g_api.read &&
      g_api.reg(overlay_cb, NULL) == 0)
    g_use_api = 1;
  rsm_log("overlay: %s", g_use_api ? "overlay API" : "OpenGL (overlay API incomplete)");
  return g_use_api;
}

// --- old builds: OpenGL ---

void rsm_overlay_draw(void) {
  if (use_api()) return;  // overlay_cb draws, later in this frame
  int n = frame_start();
  int W, H;
  if ((!n && !*g_shot_path) || !window_size(&W, &H)) return;
  if (!n) {
    take_screenshot(W, H);
    *g_shot_path = 0;
    return;
  }

  // --- save the game's state ---
  GLint fb, program, active_tex, unpack_buf, viewport[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &fb);
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active_tex);
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buf);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glActiveTexture(GL_TEXTURE0);  // the texture matrix pushed and popped below is unit 0's
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
  glMatrixMode(GL_TEXTURE);
  glPushMatrix();
  glLoadIdentity();
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
  glUseProgram(0);
  for (int u = 7; u >= 0; u--) {  // only unit 0, and only its 2D texture
    glActiveTexture(GL_TEXTURE0 + u);
    glDisable(GL_TEXTURE_1D);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_3D);
    glDisable(GL_TEXTURE_CUBE_MAP);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  // the upload reads the bitmap as it is, and copies it (no client storage: the bitmap is freed after it)
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
  glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
  glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
  glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
  glViewport(0, 0, W, H);
  glMatrixMode(GL_PROJECTION);
  glOrtho(0, W, H, 0, -1, 1);
  glMatrixMode(GL_MODELVIEW);
  glDisable(GL_VERTEX_PROGRAM_ARB);
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_ALPHA_TEST);
  glDisable(GL_LIGHTING);
  glDisable(GL_FOG);
  glDisable(GL_TEXTURE_GEN_S);
  glDisable(GL_TEXTURE_GEN_T);
  glDisable(GL_COLOR_LOGIC_OP);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  // the bitmaps are premultiplied
  glEnable(GL_TEXTURE_2D);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  glColor4f(1, 1, 1, 1);

  for (int i = 0; i < n; i++) {
    float x, y;
    cached *e = place(&g_items[i], W, H, &x, &y);
    if (!e) continue;
    glBindTexture(GL_TEXTURE_2D, e->tex);
    glBegin(GL_QUADS);  // a bitmap context's memory starts with its top row
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + e->w, y);
    glTexCoord2f(1, 1); glVertex2f(x + e->w, y + e->h);
    glTexCoord2f(0, 1); glVertex2f(x, y + e->h);
    glEnd();
  }

  // --- restore ---
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_TEXTURE);
  glPopMatrix();
  glPopClientAttrib();
  glPopAttrib();
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)unpack_buf);
  glActiveTexture((GLenum)active_tex);
  glUseProgram((GLuint)program);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, (GLuint)fb);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  if (*g_shot_path) {
    take_screenshot(W, H);
    *g_shot_path = 0;
  }
}
