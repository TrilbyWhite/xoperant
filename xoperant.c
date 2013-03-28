/**************************************************************************\
* XOPERANT - Control software for McCluresK9 operant boxes
*
* Author: Jesse McClure, copyright 2012-2013
* License: GPLv3
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*
\**************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <phidget21.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>

#define DATAPATH	"/mnt/data/"
#define TILING_WM	False

typedef CPhidgetInterfaceKitHandle IFKh;
typedef CPhidgetHandle Ph;

#define STATE_OFF	0
#define STATE_LEFT	1
#define STATE_RIGHT	2
#define STATE_BOTH	3
#define STATE_FORCE	4

#define IN_STATE(x,y)	((box[x].state & y) == y)

#define N_PERCH		2
typedef struct _Box {
	IFKh	ifk;
	int		n;
	int		serial;
	int		state;
	char	*song[N_PERCH];
	time_t	last_play[N_PERCH];
	FILE	*data;
	int		trial;
	int		session;
	int		total[N_PERCH];
} Box;

static Box		*box;
static int		box_count;
static FILE		*log;
static int		check_interval;
static time_t	start_time;
static time_t	session_len;
static time_t	inter_session;
static time_t	time_stamp;
static int		timeout;
static int		forced_trials = 0;
static int		free_trials;
static int		*random_states;
static char		*song_path;
static char		logline[256] = "Welcome to XOperant - Connecting to phidgets...";
static char		cmd_str[256] = "";

static void buttonpress(XEvent *);
static void command_line(int,const char **);
static void die(const char *,...);
static void draw();
static void expose(XEvent *);
static void keypress(XEvent *);
static void log_entry(const char *,...);
static void read_rc();
static void set_color(const char *);
static void set_state(int,int);

static Display *dpy;
static int screen, sw, sh, ww, wh;
static Window root, win;
static Pixmap buf;
static Bool running=False;
static GC gc;
static Colormap cmap;
static XFontStruct *fontstruct;
static int fontheight;
static const char font[] = "-*-terminus-bold-r-*--12-120-72-72-c-60-*-*";
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress]	= buttonpress,
	[Expose]		= expose,
};

void buttonpress(XEvent *e) {
	if (e->xbutton.button != 1) return;
	int x = e->xbutton.x, y = e->xbutton.y;
	if (x > 523 && x < 623 && y > 442 && y < 462)
		running = False;
	else if (x < 20 || x > 30) return;
	int i,side;
	if (y > 45 && y < 55)			{i=0; side=0;}
	else if (y > 65 && y < 75)		{i=0; side=1;}
	else if (y > 95 && y < 105)		{i=1; side=0;}
	else if (y > 115 && y < 125)	{i=1; side=1;}
	else if (y > 145 && y < 155)	{i=2; side=0;}
	else if (y > 165 && y < 175)	{i=2; side=1;}
	else if (y > 195 && y < 205)	{i=3; side=0;}
	else if (y > 215 && y < 225)	{i=3; side=1;}
	else return;
	log_entry("Manual trigger %d in box %d, song triggered\n",side,i+1);
	sprintf(cmd_str,"aplay -D Out%d /mnt/songs/%s >/dev/null 2>&1 &",
		i*N_PERCH+side,box[i].song[side]);
	log_entry("SYS: %s\n",cmd_str);
	system(cmd_str);
}

void command_line(int argc, const char **argv) {
	char *rcfile = NULL;
	char *logfile = NULL;
	char *home = getenv("HOME");
	int i,j=-1;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			/* TODO options */
		}
		else j=i;
	}
	if (j == -1) {
		rcfile = (char *) calloc(strlen(home) + 13, sizeof(char));
		strcpy(rcfile,home);
		strcat(rcfile,"/.xoperantrc");
	}
	else {
		rcfile = (char *) calloc(strlen(argv[j]) + 1, sizeof(char));
		strcpy(rcfile,argv[j]);
	}
	if (!logfile) {
		logfile = (char *) calloc(strlen(home) + 14, sizeof(char));
		strcpy(logfile,home);
		strcat(logfile,"/.xoperantlog");
		log = fopen(logfile,"a");
		free(logfile);
	}	
	read_rc(rcfile);
	free(rcfile);
}

void die(const char *msg,...) {
	va_list args;
	va_start(args,msg);
	vfprintf(stderr,msg,args);
	if (running) {
		vsprintf(logline,msg,args);
		char *c = strchr(logline,'\n');
		if (c) *c='\0';
		set_color("#FFFFFF");
		XFillRectangle(dpy,win,gc,15,440,610,25);
		set_color("#FF0000");
		XDrawString(dpy,win,gc,20,450+fontheight,"ERROR:",6);
		XDrawString(dpy,win,gc,62,450+fontheight,logline,strlen(logline));
		XDrawString(dpy,win,gc,480,450+fontheight,"PRESS ANY KEY TO CLOSE",22);
		XFlush(dpy);
		XEvent e;
		while ( !XNextEvent(dpy,&e) && e.type != KeyPress );
	}
	va_end(args);
	exit(1);
}

void draw() {
	set_color("#bbbbbb");
	XFillRectangle(dpy,buf,gc,0,0,640,480);
	set_color("#ffffff");
		XFillRectangle(dpy,buf,gc,10,10,620,460);
	set_color("#484848");
		XDrawLine(dpy,buf,gc,10,10,630,10);
		XDrawLine(dpy,buf,gc,11,11,630,11);
		XDrawLine(dpy,buf,gc,630,10,630,470);
		XDrawLine(dpy,buf,gc,629,10,629,469);
	set_color("#888888");
		XDrawLine(dpy,buf,gc,10,10,10,470);
		XDrawLine(dpy,buf,gc,11,11,11,470);
		XDrawLine(dpy,buf,gc,11,470,630,470);
		XDrawLine(dpy,buf,gc,11,469,630,469);
	set_color("#000000");
		XDrawRectangle(dpy,buf,gc,12,12,616,456);
	XDrawString(dpy,buf,gc,25,18+fontheight,"*",1);
	XDrawString(dpy,buf,gc,45,18+fontheight,"SONG",4);
	XDrawString(dpy,buf,gc,405,18+fontheight,"COUNT",5);
	XDrawString(dpy,buf,gc,535,18+fontheight,"METER",5);
	XDrawLine(dpy,buf,gc,20,35,620,35);
	int i,j,k,y=35;
	float fj,fk;
	char str[16];
	for (i = 0; i < box_count; i++) {
		if IN_STATE(i,STATE_LEFT) set_color("#FFDD0E");
		else set_color("#999999");
		XFillRectangle(dpy,buf,gc,20,10+y,10,10);
		XDrawString(dpy,buf,gc,40,8+y+fontheight,box[i].song[0],
			strlen(box[i].song[0]));
		if IN_STATE(i,STATE_RIGHT) set_color("#FFDD0E");
		else set_color("#999999");
		XFillRectangle(dpy,buf,gc,20,30+y,10,10);
		XDrawString(dpy,buf,gc,40,28+y+fontheight,box[i].song[1],
			strlen(box[i].song[1]));
		set_color("#000000");
		XDrawRectangle(dpy,buf,gc,20,10+y,10,10);
		XDrawRectangle(dpy,buf,gc,20,30+y,10,10);
		XDrawLine(dpy,buf,gc,20,50+y,620,50+y);
		/* meters */
		j = box[i].total[0];
		k = box[i].total[1];
		sprintf(str,"(%d/%d)",j,j+k);
		XDrawString(dpy,buf,gc,400,8+y+fontheight,str,strlen(str));
		sprintf(str,"(%d/%d)",k,j+k);
		XDrawString(dpy,buf,gc,400,28+y+fontheight,str,strlen(str));
		fj = (float)j / (float)(j + k);
		fk = (float)k / (float)(j + k);
		if (j+k > 1) {
			if (fj > 0.80) {
				set_color("#44ff44");
				XFillRectangle(dpy,buf,gc,500,10+y,100*fj,30);
				set_color("#ff4444");
				XFillRectangle(dpy,buf,gc,500+100*fj+1,10+y,100*fk,30);
			}
			else if (fj > 0.2) {
				set_color("#4444ff");
				XFillRectangle(dpy,buf,gc,500,10+y,100,30);
			}
			else {
				set_color("#ff4444");
				XFillRectangle(dpy,buf,gc,500,10+y,100*fj,30);
				set_color("#44ff44");
				XFillRectangle(dpy,buf,gc,500+100*fj+1,10+y,100*fk,30);
			}
			set_color("#000000");
			XDrawRectangle(dpy,buf,gc,500,10+y,100,30);
			set_color("#FFDD0E");
			XFillRectangle(dpy,buf,gc,498+100*fj,5+y,4,40);
			set_color("#000000");
			XDrawRectangle(dpy,buf,gc,498+100*fj,5+y,4,40);
		}	
		else {
			set_color("#FFFF88");
			XFillRectangle(dpy,buf,gc,500,10+y,100,30);
			set_color("#000000");
			XDrawRectangle(dpy,buf,gc,500,10+y,100,30);
			XDrawString(dpy,buf,gc,510,20+y+fontheight,"No data...",10);
		}
		y+=50;
	}
	set_color("#BBBBBB");
	XFillRectangle(dpy,buf,gc,523,442,100,20);
	XDrawString(dpy,buf,gc,20,450+fontheight,"LOG:",4);
	XDrawString(dpy,buf,gc,50,450+fontheight,logline,strlen(logline));
	set_color("#999999");
		XDrawLine(dpy,buf,gc,524,443,622,443);
		XDrawLine(dpy,buf,gc,525,444,622,444);
		XDrawLine(dpy,buf,gc,622,443,622,461);
		XDrawLine(dpy,buf,gc,621,443,621,460);
	set_color("#626262");
		XDrawLine(dpy,buf,gc,524,443,524,461);
		XDrawLine(dpy,buf,gc,525,444,525,461);
		XDrawLine(dpy,buf,gc,524,461,622,461);
		XDrawLine(dpy,buf,gc,524,460,621,460);
	set_color("#000000");
	XDrawRectangle(dpy,buf,gc,523,442,100,20);
	XDrawString(dpy,buf,gc,560,445+fontheight,"EXIT",4);
	XCopyArea(dpy,buf,win,gc,0,0,640,480,0,0);
	XFlush(dpy);
}

void expose(XEvent *e) {
	draw();
}

void log_entry(const char *msg,...) {
	va_list args;
	va_start(args,msg);
	fprintf(log,"%d: ",time(NULL));
	vfprintf(log,msg,args);
	vsprintf(logline,msg,args);
	char *c = strchr(logline,'\n');
	if (c) *c='\0';
	va_end(args);
	if (running) draw();
}

int ph_error(Ph ifk,void *uptr,int err,const char *msg) {
	log_entry("ERROR %d: %s\n",err,msg);
	return 0;
}

int ph_sensor(IFKh ifk, void *uptr, int index, int value) {
	if (value > 0) return 0;
	int i = *(int *)uptr;
	int side = index;
	time_stamp = time(NULL);
	if IN_STATE(i,side+1) {
		if (time_stamp > box[i].last_play[side] + timeout) {
			log_entry("Response %d in box %d, song triggered\n",side,i+1);
			sprintf(cmd_str,"aplay -D Out%d /mnt/songs/%s >/dev/null 2>&1 &",
				i*N_PERCH+side,box[i].song[side]);
			log_entry("SYS: %s\n",cmd_str);
			system(cmd_str);
			box[i].last_play[side] = time(NULL);
			box[i].total[side]++;
			box[i].trial++;
			if IN_STATE(i,STATE_BOTH) { /* free choice trial */
				fprintf(box[i].data,"%d,%d,%s\n",
					time_stamp-start_time,side,box[i].song[side]);
				if (box[i].trial >= free_trials) {
					box[i].trial = 0;
					set_state(i,STATE_OFF);
				}
			}
			else { /* forced trial */
				fprintf(box[i].data,"%d,%d,FORCED: %s\n",
					time_stamp-start_time,side,box[i].song[side]);
				set_state(i,random_states[box[i].trial]);
				if (box[i].trial >= forced_trials) box[i].trial = 0;
			}
		}
		else
			log_entry("Response %d in box %d (DURING TIMEOUT)\n",side,i+1);
	}
	else
		log_entry("Response %d in box %d (INACTIVE)\n",side,i+1);
}

void read_rc(const char *filename) {
	char line[256];
	char str[120];
	char *c;
	int i=0,j;
	int session_minutes = 30;
	FILE *rc = fopen(filename,"r");
	if (!rc) die("Error opening config file \"%s\"\n",filename);
	while (fgets(line,255,rc)) if (strncmp(line,"box {",5) == 0) box_count++;
	box = (Box *) calloc(box_count,sizeof(Box));
	fseek(rc,0,SEEK_SET);
	while (fgets(line,255,rc)) {
		if (line[0] == '\n' || line[0] == '#') continue;
		if (strncmp(line,"box {",5)==0) {
			j = 0;
			while (fgets(line,255,rc)) {
				if (line[0] == '}') {
					box[i].n = i; i++;
					break;
				}
				else if (sscanf(line," song %s",str) == 1) {
					c = strchr(str,'\n');
					if (c) *c='\0';
					box[i].song[j] = (char *) calloc(strlen(str)+1,sizeof(char));
					strcpy(box[i].song[j++],str);
				}
				else if (sscanf(line," serial %d",&box[i].serial) == 1);
			}
		}
		else if (!	sscanf(line,"set duration %d",&session_len)	&&
				!	sscanf(line,"set break %d",&inter_session)	&&
				!	sscanf(line,"set timeout %d",&timeout)		&&
				!	sscanf(line,"set forced %d",&forced_trials)	&&
				!	sscanf(line,"set free %d",&free_trials)		)
			log_entry("faulty config entry: %s\n",line);
	}
	struct tm *tinfo;
	time(&time_stamp);
	tinfo=localtime(&time_stamp);
	for (i = 0; i < box_count; i++) {
		strftime(str,20,"%d%b%Y_%H%M_",tinfo);
		sprintf(line,DATAPATH "%sbox%d.csv",str,i+1);
		box[i].data = fopen(line,"w");
	}
	session_len = session_len * 60;
	inter_session = inter_session * 60;
}

void set_color(const char *colstring) {
	XColor color;
	XAllocNamedColor(dpy,cmap,colstring,&color,&color);
	XSetForeground(dpy,gc,color.pixel);
}

void set_state(int i,int state) {
	box[i].state = state;
	CPhidgetInterfaceKit_setOutputState(box[i].ifk,0,IN_STATE(i,STATE_LEFT));
	CPhidgetInterfaceKit_setOutputState(box[i].ifk,1,IN_STATE(i,STATE_RIGHT));
	draw();
}

int main(int argc, const char **argv) {
	XInitThreads();
	command_line(argc,argv);
/* MAIN: INIT XLIB */
	if (!(dpy = XOpenDisplay(0x0))) die("no X display detected\n");
	screen = DefaultScreen(dpy);
	sw = DisplayWidth(dpy,screen);
	sh = DisplayHeight(dpy,screen);
	root = RootWindow(dpy,screen);
	cmap = DefaultColormap(dpy,screen);
	XGCValues val;
	val.font = XLoadFont(dpy,font);
	gc = XCreateGC(dpy,root,GCFont,&val);
	fontstruct = XQueryFont(dpy,val.font);
	fontheight = fontstruct->ascent+1;
	win = XCreateSimpleWindow(dpy,root,40,40,640,480,0,0,0);
	buf = XCreatePixmap(dpy,root,640,480,DefaultDepth(dpy,screen));
	XStoreName(dpy,win,"Operant");
	XSetWindowAttributes wa;
	wa.event_mask = ExposureMask|KeyPressMask|ButtonPressMask;
	wa.override_redirect = TILING_WM;
	XChangeWindowAttributes(dpy,win,CWEventMask|CWOverrideRedirect,&wa);
	XMapWindow(dpy,win);
	draw();
	running = True;
	XFlush(dpy);
/* MAIN: INIT PHIDGETS */
	log_entry("#### START SESSION ####\n");
	time_stamp = time(NULL);
	struct tm *tinfo = localtime(&time_stamp);
	log_entry("## time stamp: %s",asctime(tinfo));
	int i,j;
	const char *err;
	random_states = (int *) calloc(forced_trials+1,sizeof(int));
	srand(time(NULL));
	/* generate random_states */
	for (i = 0; i < forced_trials; i++)
		random_states[i] = rand() % 2 + 1;
	for (i = 0; i < forced_trials / 2; i++)
		random_states[i+forced_trials / 2] = (random_states[i] == 1 ? 2 : 1);
	random_states[forced_trials] = STATE_BOTH;
	/* connect to phidgets */
	for (i = 0; i < box_count; i++) {
		CPhidgetInterfaceKit_create(&box[i].ifk);
		CPhidget_set_OnError_Handler((Ph) box[i].ifk,ph_error,NULL);
		CPhidgetInterfaceKit_set_OnSensorChange_Handler((IFKh) box[i].ifk,
			ph_sensor,&box[i].n);
		CPhidget_open((Ph)box[i].ifk,box[i].serial);
		if ((j = CPhidget_waitForAttachment((Ph)box[i].ifk,4000))) {
			CPhidget_getErrorDescription(j,&err);
			log_entry("connection error %d: %s\n",j,err);
			log_entry("####  END SESSION  ####\n\n");
			die("connection error %d: %s\n",j,err);
		}
		log_entry("OPEN box %d [handle=%d serial=%d]\n",i,box[i].ifk,box[i].serial);
		fprintf(box[i].data,"time,response,song\n");
	}
	start_time = time(NULL);
/* MAIN: MAIN LOOP */
	XEvent ev;
	int xfd,r;
	struct timeval tv;
	fd_set rfd;
	xfd = ConnectionNumber(dpy);
	start_time = time(NULL);
	for (i = 0; i < box_count; i++) {
		if (forced_trials > 0)
			set_state(i,random_states[0]);
		else 
			set_state(i,STATE_BOTH);
	}
	while (running) {
		memset(&tv,0,sizeof(tv));
		tv.tv_sec = 1;
		FD_ZERO(&rfd);
		FD_SET(xfd,&rfd);
		r = select(xfd+1,&rfd,0,0,&tv);
		time_stamp = time(NULL);
		if (r == 0) { /* timer event */
			if (start_time + session_len < time_stamp) break;
			for (i = 0; i < box_count; i++) if ( box[i].state == STATE_OFF && 
					box[i].last_play[0] + inter_session > time_stamp )
				set_state(i,random_states[0]);
		}
		if (FD_ISSET(xfd,&rfd)) while (XPending(dpy)) {
			XNextEvent(dpy,&ev);
			if (handler[ev.type]) handler[ev.type](&ev);
		}
	}
/* MAIN: CLEANUP */
	for (i = 0; i < box_count; i++) {
		CPhidget_close((Ph)box[i].ifk);
		log_entry("CLOSE box %d [handle=%d]\n",i,box[i].ifk);
		CPhidget_delete((Ph)box[i].ifk);
		for (j = 0; j < N_PERCH; j++) free(box[i].song[j]);
		fclose(box[i].data);
	}
	log_entry("####  END SESSION  ####\n");
	fclose(log);
	free(box);
	free(random_states);
	//XFreeFont(dpy,fontstruct);
	XUnloadFont(dpy,val.font);
	XCloseDisplay(dpy);
}


