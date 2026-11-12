/* gfx.c - framebuffer graphics with an in-memory canvas
 *
 * Everything is drawn into a 32-bit RGB canvas.  When /dev/fb0 is
 * available the canvas is blitted to it (RGB565 or 32bpp) at most once
 * per frame; otherwise drawing still works and can be written out with
 * SAVE IMAGE, which is what makes graphics testable over SSH.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#ifdef __linux__
#include <linux/fb.h>
#endif
#include "shk.h"
#include "font6x8.h"

#define DEF_W 480
#define DEF_H 320

static uint32_t *canvas = NULL;
static int   cw = 0, ch_ = 0;
static int   inited = 0;

static int      fbfd = -1;
static uint8_t *fbmem = NULL;
static size_t   fblen = 0;
static int      fb_bpp = 0, fb_stride = 0, fb_w = 0, fb_h = 0;
static int      fb_roff = 16, fb_goff = 8, fb_boff = 0;

static int   dirty = 0;
static double last_present_ms = 0;

static double now_ms(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

int gfx_available(void) { return inited; }
int gfx_width(void)  { return inited ? cw : DEF_W; }
int gfx_height(void) { return inited ? ch_ : DEF_H; }

/* ------------------------------------------------------------------ */
static void fb_open(void)
{
#ifdef __linux__
  struct fb_var_screeninfo v;
  struct fb_fix_screeninfo f;
  const char *dev = getenv("SHKBASIC_FB");
  if (!dev) dev = "/dev/fb0";

  fbfd = open(dev, O_RDWR);
  if (fbfd < 0) return;
  if (ioctl(fbfd, FBIOGET_VSCREENINFO, &v) < 0 ||
      ioctl(fbfd, FBIOGET_FSCREENINFO, &f) < 0) {
    close(fbfd); fbfd = -1; return;
  }
  fb_w = (int)v.xres;
  fb_h = (int)v.yres;
  fb_bpp = (int)v.bits_per_pixel;
  fb_stride = (int)f.line_length;
  fb_roff = v.red.offset;
  fb_goff = v.green.offset;
  fb_boff = v.blue.offset;
  if (fb_bpp != 16 && fb_bpp != 24 && fb_bpp != 32) {
    close(fbfd); fbfd = -1; return;
  }
  fblen = (size_t)fb_stride * (size_t)fb_h;
  fbmem = mmap(NULL, fblen, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
  if (fbmem == MAP_FAILED) { fbmem = NULL; close(fbfd); fbfd = -1; }
#endif
}

int gfx_init(int want_fb)
{
  if (inited) return 1;
  if (want_fb && !getenv("SHKBASIC_NOFB")) fb_open();
  cw = fbmem ? fb_w : DEF_W;
  ch_ = fbmem ? fb_h : DEF_H;
  if (cw <= 0 || cw > 4096) cw = DEF_W;
  if (ch_ <= 0 || ch_ > 4096) ch_ = DEF_H;
  canvas = calloc((size_t)cw * (size_t)ch_, sizeof(uint32_t));
  if (!canvas) return 0;
  inited = 1;
  dirty = 1;
  return 1;
}

void gfx_shutdown(void)
{
  if (!inited) return;
  gfx_present();
  free(canvas);
  canvas = NULL;
#ifdef __linux__
  if (fbmem) { munmap(fbmem, fblen); fbmem = NULL; }
  if (fbfd >= 0) { close(fbfd); fbfd = -1; }
#endif
  inited = 0;
}

/* ------------------------------------------------------------------ */
void gfx_present(void)
{
  if (!inited || !fbmem) { dirty = 0; return; }
  {
    int y, x;
    int h = ch_ < fb_h ? ch_ : fb_h;
    int w = cw < fb_w ? cw : fb_w;
    for (y = 0; y < h; y++) {
      uint8_t *row = fbmem + (size_t)y * (size_t)fb_stride;
      uint32_t *src = canvas + (size_t)y * (size_t)cw;
      if (fb_bpp == 16) {
        uint16_t *d = (uint16_t *)row;
        for (x = 0; x < w; x++) {
          uint32_t c = src[x];
          d[x] = (uint16_t)((((c >> 16) & 0xF8) << 8) |
                            (((c >> 8)  & 0xFC) << 3) |
                            (( c        & 0xF8) >> 3));
        }
      } else if (fb_bpp == 32) {
        uint32_t *d = (uint32_t *)row;
        for (x = 0; x < w; x++) {
          uint32_t c = src[x];
          d[x] = (uint32_t)((((c >> 16) & 0xFF) << fb_roff) |
                            (((c >> 8)  & 0xFF) << fb_goff) |
                            (( c        & 0xFF) << fb_boff));
        }
      } else {  /* 24 */
        for (x = 0; x < w; x++) {
          uint32_t c = src[x];
          row[x*3+0] = (uint8_t)(c & 0xFF);
          row[x*3+1] = (uint8_t)((c >> 8) & 0xFF);
          row[x*3+2] = (uint8_t)((c >> 16) & 0xFF);
        }
      }
    }
  }
  dirty = 0;
  last_present_ms = now_ms();
}

static void mark(void)
{
  dirty = 1;
  if (fbmem && now_ms() - last_present_ms > 16.0) gfx_present();
}

/* ------------------------------------------------------------------ */
void gfx_cls(uint32_t rgb)
{
  int i, n;
  if (!inited) return;
  n = cw * ch_;
  for (i = 0; i < n; i++) canvas[i] = rgb;
  dirty = 1;
  gfx_present();
}

void gfx_pixel(int x, int y, uint32_t rgb)
{
  if (!inited) return;
  if (x < 0 || y < 0 || x >= cw || y >= ch_) return;
  canvas[(size_t)y * cw + x] = rgb;
  mark();
}

uint32_t gfx_getpixel(int x, int y)
{
  if (!inited || x < 0 || y < 0 || x >= cw || y >= ch_) return 0;
  return canvas[(size_t)y * cw + x];
}

static void putpx(int x, int y, uint32_t c)
{
  if (x < 0 || y < 0 || x >= cw || y >= ch_) return;
  canvas[(size_t)y * cw + x] = c;
}

static void hline(int x0, int x1, int y, uint32_t c)
{
  int x;
  if (y < 0 || y >= ch_) return;
  if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
  if (x1 < 0 || x0 >= cw) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= cw) x1 = cw - 1;
  for (x = x0; x <= x1; x++) canvas[(size_t)y * cw + x] = c;
}

static void line_raw(int x0, int y0, int x1, int y1, uint32_t rgb)
{
  int dx, dy, sx, sy, err;
  dx = abs(x1 - x0);
  dy = -abs(y1 - y0);
  sx = x0 < x1 ? 1 : -1;
  sy = y0 < y1 ? 1 : -1;
  err = dx + dy;
  for (;;) {
    putpx(x0, y0, rgb);
    if (x0 == x1 && y0 == y1) break;
    {
      int e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
}

void gfx_line(int x0, int y0, int x1, int y1, uint32_t rgb)
{
  if (!inited) return;
  line_raw(x0, y0, x1, y1, rgb);
  mark();
}

void gfx_box(int x, int y, int w, int h, int fill, uint32_t rgb, uint32_t fc)
{
  int j;
  if (!inited) return;
  if (w < 0) { x += w; w = -w; }
  if (h < 0) { y += h; h = -h; }
  if (w == 0 || h == 0) return;
  if (fill) for (j = y; j < y + h; j++) hline(x, x + w - 1, j, fc);
  hline(x, x + w - 1, y, rgb);
  hline(x, x + w - 1, y + h - 1, rgb);
  for (j = y; j < y + h; j++) { putpx(x, j, rgb); putpx(x + w - 1, j, rgb); }
  mark();
}

void gfx_rbox(int x, int y, int w, int h, int r, int fill, uint32_t rgb, uint32_t fc)
{
  int dx, dy, j;
  if (!inited) return;
  if (w < 0) { x += w; w = -w; }
  if (h < 0) { y += h; h = -h; }
  if (r < 0) r = 0;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;

  if (fill) {
    for (j = y + r; j < y + h - r; j++) hline(x, x + w - 1, j, fc);
    for (dy = 0; dy < r; dy++) {
      int dxs = (int)(sqrt((double)(r*r - (r-dy)*(r-dy))) + 0.5);
      hline(x + r - dxs, x + w - 1 - r + dxs, y + dy, fc);
      hline(x + r - dxs, x + w - 1 - r + dxs, y + h - 1 - dy, fc);
    }
  }
  hline(x + r, x + w - 1 - r, y, rgb);
  hline(x + r, x + w - 1 - r, y + h - 1, rgb);
  for (j = y + r; j < y + h - r; j++) { putpx(x, j, rgb); putpx(x + w - 1, j, rgb); }
  /* corner arcs */
  for (dy = 0; dy <= r; dy++) {
    int dxs = (int)(sqrt((double)(r*r - (r-dy)*(r-dy))) + 0.5);
    dx = r - dxs;
    putpx(x + dx, y + dy, rgb);
    putpx(x + w - 1 - dx, y + dy, rgb);
    putpx(x + dx, y + h - 1 - dy, rgb);
    putpx(x + w - 1 - dx, y + h - 1 - dy, rgb);
  }
  mark();
}

void gfx_circle(int x, int y, int r, int fill, uint32_t rgb, uint32_t fc, double aspect)
{
  int ry, dy, i, steps;
  int px = 0, py = 0;

  if (!inited || r < 0) return;
  if (aspect <= 0) aspect = 1.0;
  ry = (int)(r * aspect + 0.5);
  if (ry < 0) ry = 0;

  if (fill) {
    for (dy = -ry; dy <= ry; dy++) {
      double t = ry ? (double)dy / (double)ry : 0.0;
      double k = 1.0 - t * t;
      int dx = k > 0 ? (int)(r * sqrt(k) + 0.5) : 0;
      hline(x - dx, x + dx, y + dy, fc);
    }
  }

  /* outline as a closed polyline: enough segments that each is about
   * one pixel long, which avoids the seams a mixed scanline/arc
   * approach leaves where the curve runs near-horizontal            */
  steps = 4 * (r + ry);
  if (steps < 24) steps = 24;
  if (steps > 4096) steps = 4096;
  for (i = 0; i <= steps; i++) {
    double a = 2.0 * M_PI * (double)i / (double)steps;
    int qx = x + (int)floor(r * cos(a) + 0.5);
    int qy = y + (int)floor(ry * sin(a) + 0.5);
    if (i) line_raw(px, py, qx, qy, rgb);
    px = qx;
    py = qy;
  }
  mark();
}

void gfx_arc(int x, int y, int r0, int r1, double a0, double a1, uint32_t rgb)
{
  double a;
  int r;
  if (!inited) return;
  if (r1 < r0) { int t = r0; r0 = r1; r1 = t; }
  if (r0 < 0) r0 = 0;
  if (a1 < a0) { double t = a0; a0 = a1; a1 = t; }
  for (r = r0; r <= r1; r++) {
    double stepa = r > 0 ? (60.0 / r) : 10.0;
    if (stepa > 5) stepa = 5;
    if (stepa < 0.2) stepa = 0.2;
    for (a = a0; a <= a1; a += stepa) {
      /* MMBasic measures arc angles clockwise from 12 o'clock */
      double rad = (a - 90.0) * M_PI / 180.0;
      putpx(x + (int)(r * cos(rad) + 0.5), y + (int)(r * sin(rad) + 0.5), rgb);
    }
  }
  mark();
}

void gfx_triangle(int x0,int y0,int x1,int y1,int x2,int y2,int fill,uint32_t rgb,uint32_t fc)
{
  if (!inited) return;
  if (fill) {
    int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
    int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
    int y;
    for (y = miny; y <= maxy; y++) {
      int xs[3], n = 0;
      int ex[3][4] = { {x0,y0,x1,y1}, {x1,y1,x2,y2}, {x2,y2,x0,y0} };
      int e;
      for (e = 0; e < 3; e++) {
        int ax = ex[e][0], ay = ex[e][1], bx = ex[e][2], by = ex[e][3];
        if ((ay <= y && by > y) || (by <= y && ay > y)) {
          double t = (double)(y - ay) / (double)(by - ay);
          xs[n++] = ax + (int)(t * (bx - ax));
        }
      }
      if (n >= 2) {
        int lo = xs[0] < xs[1] ? xs[0] : xs[1];
        int hi = xs[0] > xs[1] ? xs[0] : xs[1];
        hline(lo, hi, y, fc);
      }
    }
  }
  gfx_line(x0,y0,x1,y1,rgb);
  gfx_line(x1,y1,x2,y2,rgb);
  gfx_line(x2,y2,x0,y0,rgb);
  mark();
}

/* ------------------------------------------------------------------ */
void gfx_text(int x, int y, const char *s, const char *align, int scale,
              uint32_t fg, uint32_t bg)
{
  int n, tw, th, i, gx, gy, sx, sy;
  char ha = 'L', va = 'T';

  if (!inited || !s) return;
  if (scale < 1) scale = 1;
  if (scale > 16) scale = 16;
  n = (int)strlen(s);
  tw = n * FONT_W * scale;
  th = FONT_H * scale;

  if (align && align[0]) {
    char a0 = (char)toupper((unsigned char)align[0]);
    if (a0 == 'L' || a0 == 'C' || a0 == 'R') ha = a0;
    if (align[1]) {
      char a1 = (char)toupper((unsigned char)align[1]);
      if (a1 == 'T' || a1 == 'M' || a1 == 'B') va = a1;
    }
  }
  if (ha == 'C') x -= tw / 2;
  else if (ha == 'R') x -= tw;
  if (va == 'M') y -= th / 2;
  else if (va == 'B') y -= th;

  for (i = 0; i < n; i++) {
    int c = (unsigned char)s[i];
    const unsigned char *g;
    if (c < FONT_FIRST || c > FONT_LAST) c = '?';
    g = font6x8[c - FONT_FIRST];
    for (gy = 0; gy < FONT_H; gy++) {
      for (gx = 0; gx < FONT_W; gx++) {
        int on = (gx < 8) && (g[gy] & (0x80 >> gx));
        uint32_t col = on ? fg : bg;
        if (!on && bg == 0xFF000000u) continue;      /* transparent */
        for (sy = 0; sy < scale; sy++)
          for (sx = 0; sx < scale; sx++)
            putpx(x + (i * FONT_W + gx) * scale + sx, y + gy * scale + sy, col);
      }
    }
  }
  mark();
}

/* ------------------------------------------------------------------ */
int gfx_save_bmp(const char *path)
{
  FILE *f;
  int x, y;
  int rowbytes, pad;
  uint32_t filesize, dataoff = 54;
  unsigned char hdr[54];

  if (!inited) gfx_init(0);
  if (!inited) return 0;
  rowbytes = cw * 3;
  pad = (4 - (rowbytes % 4)) % 4;
  filesize = dataoff + (uint32_t)((rowbytes + pad) * ch_);

  f = fopen(path, "wb");
  if (!f) return 0;
  memset(hdr, 0, sizeof hdr);
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = (unsigned char)(filesize);
  hdr[3] = (unsigned char)(filesize >> 8);
  hdr[4] = (unsigned char)(filesize >> 16);
  hdr[5] = (unsigned char)(filesize >> 24);
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = (unsigned char)(cw);
  hdr[19] = (unsigned char)(cw >> 8);
  hdr[20] = (unsigned char)(cw >> 16);
  hdr[22] = (unsigned char)(ch_);
  hdr[23] = (unsigned char)(ch_ >> 8);
  hdr[24] = (unsigned char)(ch_ >> 16);
  hdr[26] = 1;
  hdr[28] = 24;
  fwrite(hdr, 1, sizeof hdr, f);

  for (y = ch_ - 1; y >= 0; y--) {
    for (x = 0; x < cw; x++) {
      uint32_t c = canvas[(size_t)y * cw + x];
      unsigned char px[3];
      px[0] = (unsigned char)(c & 0xFF);
      px[1] = (unsigned char)((c >> 8) & 0xFF);
      px[2] = (unsigned char)((c >> 16) & 0xFF);
      fwrite(px, 1, 3, f);
    }
    for (x = 0; x < pad; x++) fputc(0, f);
  }
  fclose(f);
  return 1;
}
