/*
 * xenon_main.c - libxenon platform layer for doomgeneric
 *
 * Video: the engine renders 320x200 XRGB; we scale it by an integer factor
 *        straight into the Xenos tiled framebuffer (same layout as console.c).
 * Input: first USB controller. Sound is not implemented.
 * WAD:   freedoom1.wad is linked into the ELF (wad.S); fopen() of any *.wad
 *        is redirected to an in-memory FILE.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <ppc/cache.h>
#include <ppc/timebase.h>
#include <xenos/xenos.h>
#include <console/console.h>
#include <usb/usbmain.h>
#include <input/input.h>
#include <time/time.h>

#include "doomgeneric.h"
#include "doomkeys.h"

#define TB_PER_MS 49875ULL	/* Xenon timebase is 49.875 MHz */

extern const uint8_t wad_data[], wad_data_end[];

FILE *__real_fopen(const char *path, const char *mode);

FILE *__wrap_fopen(const char *path, const char *mode)
{
	size_t n = strlen(path);
	if (n > 4 && !strcasecmp(path + n - 4, ".wad") && mode[0] == 'r')
		return fmemopen((void *)wad_data, wad_data_end - wad_data, "rb");
	return __real_fopen(path, mode);
}

/* ---- video ---- */

struct ati_info {
	uint32_t unknown1[4];
	uint32_t base;
	uint32_t unknown2[8];
	uint32_t width;
	uint32_t height;
} __attribute__((__packed__));

static uint32_t *fb;
static int fb_stride, fb_w, fb_h, scale, off_x, off_y;

void DG_Init(void)
{
	struct ati_info *ai = (struct ati_info *)0xec806100ULL;

	fb = (uint32_t *)(long)(ai->base | 0x80000000);
	fb_w = ai->width;
	fb_h = ai->height;
	fb_stride = ((fb_w + 31) >> 5) << 5;
	scale = fb_w / DOOMGENERIC_RESX;
	if (fb_h / DOOMGENERIC_RESY < scale)
		scale = fb_h / DOOMGENERIC_RESY;
	if (scale < 1)
		scale = 1;
	off_x = (fb_w - DOOMGENERIC_RESX * scale) / 2;
	off_y = (fb_h - DOOMGENERIC_RESY * scale) / 2;
	console_clrscr();
}

static inline uint32_t tile_index(int x, int y)
{
	return (((y >> 5) * 32 * fb_stride + ((x >> 5) << 10) + (x & 3) +
		 ((y & 1) << 2) + (((x & 31) >> 2) << 3) + (((y & 31) >> 1) << 6)) ^
		((y & 8) << 2));
}

void DG_DrawFrame(void)
{
	int sx, sy, i, j;

	for (sy = 0; sy < DOOMGENERIC_RESY; sy++) {
		const pixel_t *src = DG_ScreenBuffer + sy * DOOMGENERIC_RESX;
		for (sx = 0; sx < DOOMGENERIC_RESX; sx++) {
			uint32_t p = src[sx];
			/* XRGB -> Xenos RGBX */
			uint32_t c = ((p & 0xff) << 24) | ((p & 0xff00) << 8) |
				     ((p & 0xff0000) >> 8);
			for (j = 0; j < scale; j++)
				for (i = 0; i < scale; i++)
					fb[tile_index(off_x + sx * scale + i,
						      off_y + sy * scale + j)] = c;
		}
	}
	memdcbst(fb, fb_stride * ((fb_h + 31) & ~31) * 4);
}

/* ---- time ---- */

uint32_t DG_GetTicksMs(void)
{
	return (uint32_t)(mftb() / TB_PER_MS);
}

void DG_SleepMs(uint32_t ms)
{
	mdelay(ms);
}

void DG_SetWindowTitle(const char *title) { (void)title; }

/* ---- input ---- */

#define MAXQ 32
static struct { int pressed; unsigned char key; } q[MAXQ];
static int qr, qw;
static uint32_t held_prev;
static int weapon = 1;

static void push(int pressed, unsigned char key)
{
	int n = (qw + 1) % MAXQ;
	if (n == qr)
		return;
	q[qw].pressed = pressed;
	q[qw].key = key;
	qw = n;
}

enum { UP, DOWN, LEFT, RIGHT, A, B, X, Y, LB, RB, START, BACK, RT, LT };

static const struct { int bit; unsigned char key; } keymap[] = {
	{ UP, KEY_UPARROW },     { DOWN, KEY_DOWNARROW },
	{ LEFT, KEY_LEFTARROW }, { RIGHT, KEY_RIGHTARROW },
	{ A, KEY_USE },          { A, KEY_ENTER },
	{ B, KEY_ESCAPE },       { START, KEY_ESCAPE },
	{ Y, 'y' },              { BACK, KEY_TAB },
	{ LB, KEY_STRAFE_L },    { RB, KEY_STRAFE_R },
	{ RT, KEY_FIRE },        { LT, KEY_RSHIFT },
};

static void poll_pad(void)
{
	struct controller_data_s d;
	uint32_t now = 0;
	unsigned i;

	usb_do_poll();
	if (get_controller_data(&d, 0) <= 0)
		return;

	now |= (uint32_t)(d.up || d.s1_y > 16000) << UP;
	now |= (uint32_t)(d.down || d.s1_y < -16000) << DOWN;
	now |= (uint32_t)(d.left || d.s1_x < -16000) << LEFT;
	now |= (uint32_t)(d.right || d.s1_x > 16000) << RIGHT;
	now |= ((uint32_t)!!d.a << A) | ((uint32_t)!!d.b << B) |
	       ((uint32_t)!!d.x << X) | ((uint32_t)!!d.y << Y);
	now |= ((uint32_t)!!d.lb << LB) | ((uint32_t)!!d.rb << RB) |
	       ((uint32_t)!!d.start << START) | ((uint32_t)!!d.back << BACK);
	now |= ((uint32_t)(d.rt > 100) << RT) | ((uint32_t)(d.lt > 100) << LT);

	for (i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++) {
		uint32_t m = 1u << keymap[i].bit;
		if ((now ^ held_prev) & m)
			push(!!(now & m), keymap[i].key);
	}
	if ((now & ~held_prev) & (1u << X)) {	/* X: cycle weapons 1..7 */
		weapon = weapon % 7 + 1;
		push(1, '0' + weapon);
		push(0, '0' + weapon);
	}
	held_prev = now;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
	if (qr == qw)
		poll_pad();
	if (qr == qw)
		return 0;
	*pressed = q[qr].pressed;
	*key = q[qr].key;
	qr = (qr + 1) % MAXQ;
	return 1;
}

/* ---- entry ---- */

int main(void)
{
	char *argv[] = { "doom", "-iwad", IWAD_NAME, "-nosound", "-nomusic",
			 "-nosfx", NULL };

	xenos_init(VIDEO_MODE_AUTO);
	console_init();
	usb_init();
	usb_do_poll();

	doomgeneric_Create((int)(sizeof(argv) / sizeof(argv[0])) - 1, argv);
	for (;;)
		doomgeneric_Tick();
	return 0;
}
