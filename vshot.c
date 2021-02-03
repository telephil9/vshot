#include <u.h>
#include <libc.h>
#include <bio.h>
#include <draw.h>
#include <memdraw.h>
#include <mouse.h>
#include <keyboard.h>
#include <thread.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STBIR_MALLOC(x,u) malloc(x)
#define STBIR_FREE(x,u) free(x)
#define STBIR_ASSERT(x) assert(x)
#define NULL nil
typedef uintptr size_t;
#include "stb_image_resize.h"

typedef struct Win Win;

struct Win
{
	Channel *c;
	int id;
	char *label;
	Memimage *image;
	Image *thumbnail;
};

enum
{
	Emouse,
	Eresize,
	Ekeyboard,
	Eload,
};

enum { Mshot, Mexit };
char  *menustr[] = { "screenshot", "exit", 0 };
Menu   menu = { menustr };

Mousectl 	*mctl;
Keyboardctl	*kctl;
Channel		*loadc;
Rectangle	winr;
Rectangle	shotr;
Image		*back;
Image		*bord;
int		loading;
Win		**wins;
int		nwins;
int		nload;
int		cur;

void*
emalloc(ulong size)
{
	void *p;

	p = malloc(size);
	if(p==nil)
		sysfatal("malloc: %r");
	return p;
}

void*
erealloc(void *p, ulong size)
{
	p = realloc(p, size);
	if(p==nil)
		sysfatal("realloc: %r");
	return p;
}

void
screenshot(void)
{
	char buf[256] = {0};
	int fd;
	
	if(enter("Save as", buf, sizeof buf, mctl, kctl, nil)<=0)
		return;
	fd = create(buf, OWRITE, 0644);
	if(fd<0)
		sysfatal("open: %r");
	writememimage(fd, wins[cur]->image);
	close(fd);
}
	
void
redraw(void)
{
	Win *w;
	Point sp, p, tp;
	Rectangle r, pr;
	int pc;
	char *s;

	draw(screen, screen->r, display->white, nil, ZP);
	if(loading){
		p.x = (Dx(screen->r)-200)/2;
		p.y = (Dy(screen->r)-25)/2;
		r = rectaddpt(rectaddpt(Rect(0, 0, 200, 25), p), screen->r.min);
		s = "Loading...";
		sp.x = (Dx(screen->r)-stringwidth(font, s))/2;
		sp.y = r.min.y - font->height - 12 - screen->r.min.y;
		string(screen, addpt(screen->r.min, sp), display->black, ZP, font, "Loading...");
		if(nwins>0 && nload>0){
			pc = 200*((double)nload/nwins);
			pr = rectaddpt(rectaddpt(Rect(0, 0, pc, 25), p), screen->r.min);
			draw(screen, pr, back, nil, ZP);
		}
		border(screen, r, 2, bord, ZP);	
		flushimage(display, 1);
		return;
	}
	w = wins[cur];
	p.x = screen->r.min.x + (Dx(winr)-stringwidth(font, w->label))/2;
	p.y = screen->r.max.y - 12 - font->height;
	string(screen, p, display->black, ZP, font, w->label);
	tp.x = (Dx(screen->r)-Dx(w->thumbnail->r))/2;
	tp.y = (Dy(screen->r)-Dy(w->thumbnail->r))/2;
	r = rectaddpt(rectaddpt(w->thumbnail->r, tp), screen->r.min);
	draw(screen, r, w->thumbnail, nil, ZP);
	flushimage(display, 1);
}

void
resize(void)
{
	int fd, x, y;

	winr  = Rect(0, 0, 800, 600);
	shotr = Rect(0, 0, 600, 400);
	fd = open("/dev/wctl", OWRITE);
	if(fd<0)
		sysfatal("open: %r");
	x = (Dx(display->image->r)-Dx(winr))/2;
	y = (Dy(display->image->r)-Dy(winr))/2;
	fprint(fd, "resize -r %d %d %d %d\n", x, y, x+Dx(winr), y+Dy(winr));
	close(fd);
	redraw();
}

char*
readstr(char *f)
{
	Biobuf *bp;
	char *s;

	bp = Bopen(f, OREAD);
	if(bp==nil)
		sysfatal("Bopen: %r");
	s = Brdstr(bp, '\n', 1);
	Bterm(bp);
	return s;
}

int
readint(char *f)
{
	char *s;
	int i;

	s = readstr(f);
	if(s==nil)
		return -1;
	i = atoi(s);
	free(s);
	return i;
}

void
addwin(Win *w)
{
	int SINC = 32;

	if(nwins==0)
		wins = emalloc(SINC * sizeof *wins);
	else if(nwins % SINC == 0)
		wins = erealloc(wins, (nwins + SINC) * sizeof *wins);
	wins[nwins++] = w;
}

Rectangle
scaledrect(Rectangle r)
{
	int mw, mh, w, h;
	double a;

	mw = Dx(shotr);
	mh = Dy(shotr);
	w = Dx(r);
	h = Dy(r);
	a = ((double)w)/h;
	if(w>mw){
		w = mw;
		h = floor(w/a);
	}
	if(h>mh){
		h = mh;
		w = floor(h*a);
	}
	return Rect(0, 0, w, h);
}		

void
loadthumbproc(void *arg)
{
	Win *w;
	char *path;
	Memimage *i;
	Image *t;
	u8int *in, *out;
	Rectangle sr;
	int iw, ih, ow, oh;
	int fd, n;

	w = arg;
	path = smprint("/dev/wsys/%d/window", w->id);
	fd = open(path, OREAD);
	if(fd<0)
		sysfatal("open: %r");
	free(path);
	i = readmemimage(fd);
	if(i==nil)
		sysfatal("readmemimage: %r");
	close(fd);
	iw = Dx(i->r);
	ih = Dy(i->r);
	n = iw*ih*4;
	in = emalloc(n);
	if(unloadmemimage(i, i->r, in, n)<0)
		sysfatal("unloadmemimage: %r");
	sr = scaledrect(i->r);
	ow = Dx(sr);
	oh = Dy(sr);
	n = ow*oh*4;
	out = emalloc(n);
	stbir_resize_uint8_generic(
		in, iw, ih, iw*4,
		out, ow, oh, ow*4,
		4, 3, STBIR_FLAG_ALPHA_PREMULTIPLIED,
		STBIR_EDGE_CLAMP, STBIR_FILTER_DEFAULT, STBIR_COLORSPACE_LINEAR,
		NULL);
	t = allocimage(display, sr, screen->chan, 0, DNofill);
	if(t==nil)
		sysfatal("allocimage: %r");
	loadimage(t, sr, out, ow*oh*4);
	w->thumbnail = t;
	w->image = i;
	free(in);
	free(out);
	sendul(w->c, 1);
	threadexits(nil);
}

void
loadwin(int id)
{
	Win *w;
	char *path;

	w = emalloc(sizeof *w);
	addwin(w);
	w->c = loadc;
	w->id = id;
	path = smprint("/dev/wsys/%d/label", id);
	w->label = readstr(path);
	free(path);
	if(w->label==nil)
		sysfatal("unable to read window label: %r");
}

void
loadwins(void)
{
	int fd, i, wid, id;
	Dir *dir;
	long n;

	id = readint("/mnt/wsys/winid");
	if(id<0)
		sysfatal("unable to read /mnt/wsys/winid: %r");
	wins = emalloc(sizeof *wins);
	fd = open("/dev/wsys/", OREAD);
	if(fd<0)
		sysfatal("open: %r");
	n = dirreadall(fd, &dir);
	if(n<0)
		sysfatal("dirreadall: %r");
	for(i=0; i<n; i++){
		wid = atoi(dir[i].name);
		if(wid==id)
			continue;
		loadwin(wid);
	}
	/* start threads afterward so we are sure that nwins is final */
	for(i=0; i<nwins; i++){
		proccreate(loadthumbproc, wins[i], 4*1024);
	}
}

void
threadmain(int argc, char *argv[])
{
	USED(argc);
	USED(argv);
	Mouse m;
	Rune k;
	int n;
	vlong t0, t1;
	Alt alts[] = {
		{ nil, &m,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, &k,  CHANRCV },
		{ nil, nil, CHANRCV },
		{ nil, nil, CHANEND },
	};

	nload = 0;
	nwins = 0;
	cur = 0;
	loading = 1;
	if(initdraw(nil, nil, "vshot")<0)
		sysfatal("initdraw: %r");
	if((mctl = initmouse(nil, screen))==nil)
		sysfatal("initmouse: %r");
	if((kctl = initkeyboard(nil))==nil)
		sysfatal("initkeyboard: %r");
	if((loadc = chancreate(sizeof(ulong), 1))==nil)
		sysfatal("chancreate: %r");
	alts[Emouse].c = mctl->c;
	alts[Eresize].c = mctl->resizec;
	alts[Ekeyboard].c = kctl->c;
	alts[Eload].c = loadc;
	back = allocimagemix(display, DPalegreen, DWhite);
	bord = allocimage(display, Rect(0,0,1,1), CMAP8, 1, DMedgreen);
	memimageinit();
	resize();
	t0 = nsec();
	loadwins();
	for(;;){
		switch(alt(alts)){
		case Emouse:
			if(m.buttons&4){
				n = menuhit(3, mctl, &menu, nil);
				switch(n){
				case Mshot:
					screenshot();
					break;
				case Mexit:
					goto End;
				}
			}
			break;
		case Eresize:
			if(getwindow(display, Refnone)<0)
				sysfatal("getwindow: %r");
			resize();
			break;
		case Ekeyboard:
			switch(k){
			case 'q':
			case Kdel:
				goto End;
			case Kright:
				++cur;
				if(cur>=nwins)
					cur = 0;
				redraw();
				break;
			case Kleft:
				--cur;
				if(cur<0)
					cur = nwins - 1;
				redraw();
				break;
			}
			break;
		case Eload:
			++nload;
			if(nload==nwins){
				t1 = nsec();
				loading = 0;
			}
			redraw();
			break;
		}
	}
End:
	chanfree(loadc);
	closemouse(mctl);
	closekeyboard(kctl);
	threadexitsall(nil);
}
