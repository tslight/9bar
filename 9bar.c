/*
 * 9front's bar for X11
 *
 * usage: 9bar [-f font] [-t timefmt]
 */
#include <sys/types.h>
#include <sys/select.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#ifdef __OpenBSD__
#include <sys/ioctl.h>
#include <machine/apmvar.h>
#endif
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

enum {
	Padding = 2,
	Refresh = 60,
};

enum {
	Bg, Fg, Ncol,
};

static Display *dpy;
static int scr;
static Window win;
static Pixmap pix;
static GC gc;
static XftFont *font;
static XftDraw *draw;
static XftColor col[Ncol];
static int batt = -1;
static int onac;
static unsigned int barw, barh;
static int hidden;
static char *fontname = "monospace:bold:size=9";
static char *timefmt = "%H:%M %a %d/%m";

#ifdef __OpenBSD__
static int apmfd = -1;

static void
initbattery(void)
{
	apmfd = open("/dev/apm", O_RDONLY);
}

static void
readbattery(void)
{
	struct apm_power_info ai;

	if(apmfd < 0 || ioctl(apmfd, APM_IOC_GETPOWER, &ai) < 0){
		batt = -1;
		return;
	}
	batt = ai.battery_life <= 100 ? ai.battery_life : -1;
	onac = ai.ac_state == APM_AC_ON;
}

#elif defined(__FreeBSD__)
static void initbattery(void) {}

static void
readbattery(void)
{
	int life, ac;
	size_t len;

	len = sizeof life;
	if(sysctlbyname("hw.acpi.battery.life", &life, &len, NULL, 0) < 0){
		batt = -1;
		return;
	}
	batt = life <= 100 ? life : -1;
	len = sizeof ac;
	if(sysctlbyname("hw.acpi.acline", &ac, &len, NULL, 0) < 0)
		ac = 0;
	onac = ac;
}

#else
static void initbattery(void) {}
static void readbattery(void) { batt = -1; }
#endif

static void
mkcolor(int i, unsigned long pixel)
{
	XRenderColor rc = {0};
	rc.alpha = 0xffff;
	rc.red = (unsigned short)((pixel >> 16 & 0xff) * 257);
	rc.green = (unsigned short)((pixel >> 8 & 0xff) * 257);
	rc.blue = (unsigned short)((pixel & 0xff) * 257);
	XftColorAllocValue(dpy, DefaultVisual(dpy, scr),
		DefaultColormap(dpy, scr), &rc, &col[i]);
	XftColorAllocValue(dpy, DefaultVisual(dpy, scr),
		DefaultColormap(dpy, scr), &rc, &col[i]);
}

static int
textwidth(char *s)
{
	XGlyphInfo ext;

	XftTextExtentsUtf8(dpy, font, (FcChar8 *)s, (int)strlen(s), &ext);
	return ext.xOff;
}

static void
drawstr(XftColor *c, int x, int y, char *s)
{
	XftDrawStringUtf8(draw, c, font, x, y,
		(FcChar8 *)s, (int)strlen(s));
}

static void
redraw(void)
{
	time_t now;
	struct tm *t;
	char tbuf[64], bbuf[32];
	int x, y;
	unsigned int w, h;

	now = time(NULL);
	t = localtime(&now);
	strftime(tbuf, sizeof tbuf, timefmt, t);

	if(batt >= 0){
		snprintf(bbuf, sizeof bbuf, onac ? "%d%%" : "!%d%%", batt);
	}else{
		bbuf[0] = '\0';
	}

	w = (unsigned int)textwidth(tbuf)+Padding*2;
	if(bbuf[0])
		w += (unsigned int)textwidth(bbuf)+(unsigned int)textwidth(" ");
	h = (unsigned int)(font->ascent + font->descent) + Padding*2;
	if(w != barw || h != barh){
		barw = w;
		barh = h;
		if(pix)
			XFreePixmap(dpy, pix);
		pix = XCreatePixmap(dpy, win, barw, barh,
			(unsigned int)DefaultDepth(dpy, scr));
		XftDrawChange(draw, pix);
		XMoveResizeWindow(dpy, win,
			DisplayWidth(dpy, scr) - (int)barw,
			DisplayHeight(dpy, scr) - (int)barh, barw, barh);
	}

	XSetForeground(dpy, gc, col[Bg].pixel);
	XFillRectangle(dpy, pix, gc, 0, 0, barw, barh);

	x = Padding;
	y = Padding + font->ascent;

	if(bbuf[0]){
		drawstr(&col[Fg], x, y, bbuf);
		x += textwidth(bbuf);
		drawstr(&col[Fg], x, y, " ");
		x += textwidth(" ");
	}
	drawstr(&col[Fg], x, y, tbuf);

	XCopyArea(dpy, pix, win, gc, 0, 0, barw, barh, 0, 0);
}

static void
usage(void)
{
	fprintf(stderr, "usage: bar [-f font] [-t timefmt]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	XSetWindowAttributes wa;
	XEvent ev;
	fd_set rd;
	struct timeval tv;
	struct tm fat;
	char maxstr[128], tbuf[64];
	int xfd, ch;
	time_t now, deadline;

	while((ch = getopt(argc, argv, "f:t:")) != -1)
		switch(ch){
		case 'f': fontname = optarg; break;
		case 't': timefmt = optarg; break;
		default: usage();
		}

	initbattery();

	if(!(dpy = XOpenDisplay(NULL)))
		errx(1, "cannot open display");
	scr = DefaultScreen(dpy);
	xfd = ConnectionNumber(dpy);

	if(!(font = XftFontOpenName(dpy, scr, fontname)))
		errx(1, "cannot open font: %s", fontname);

	mkcolor(Bg, 0x9eeeee);
	mkcolor(Fg, 0x000000);

	/* worst case size */
	memset(&fat, 0, sizeof fat);
	fat.tm_mon = 8;
	fat.tm_mday = 28;
	fat.tm_hour = 20;
	fat.tm_year = 100;
	strftime(tbuf, sizeof tbuf, timefmt, &fat);
	snprintf(maxstr, sizeof maxstr, "!100%% %s", tbuf);
	barw = (unsigned int)textwidth(maxstr) + Padding*2;
	barh = (unsigned int)(font->ascent + font->descent) + Padding*2;

	wa.override_redirect = True;
	wa.background_pixel = col[Bg].pixel;
	wa.event_mask = ExposureMask | ButtonPressMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scr),
		DisplayWidth(dpy, scr) - (int)barw,
		DisplayHeight(dpy, scr) - (int)barh, barw, barh, 0,
		DefaultDepth(dpy, scr),
		InputOutput, DefaultVisual(dpy, scr),
		CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);

	gc = XCreateGC(dpy, win, 0, NULL);
	pix = XCreatePixmap(dpy, win, barw, barh,
		(unsigned int)DefaultDepth(dpy, scr));
	draw = XftDrawCreate(dpy, pix,
		DefaultVisual(dpy, scr), DefaultColormap(dpy, scr));

	XMapRaised(dpy, win);
	readbattery();
	redraw();
	XSync(dpy, False);

	deadline = time(NULL) + Refresh;
	for(;;){
		now = time(NULL);
		if(now >= deadline){
			readbattery();
			if(hidden){
				hidden = 0;
				XMapRaised(dpy, win);
			}
			redraw();
			deadline = now + Refresh;
		}
		while(XPending(dpy)){
			XNextEvent(dpy, &ev);
			if(ev.type == Expose && !hidden)
				XCopyArea(dpy, pix, win, gc,
					0, 0, barw, barh, 0, 0);
			if(ev.type == ButtonPress && ev.xbutton.button == 3){
				hidden = 1;
				XUnmapWindow(dpy, win);
			}
		}
		if(!hidden)
			XRaiseWindow(dpy, win);
		FD_ZERO(&rd);
		FD_SET(xfd, &rd);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		if(select(xfd + 1, &rd, NULL, NULL, &tv) < 0
		    && errno != EINTR)
			errx(1, "lost display connection");
	}
}
