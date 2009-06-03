#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "../gfxdevice.h"
#include "../gfxtools.h"

/* notice: left/right for a coordinate system where y goes up, not down */
typedef enum {LEFT=0, RIGHT=1} leftright_t;

/* factor that determines into how many line fragments a spline is converted */
#define SUBFRACTION (2.4)

// spline equation:
// s(t) = t*t*x2 + 2*t*(1-t)*cx + (1-t)*(1-t)*x1
//
// s(0.5) = 0.25*x2 + 0.5*cx + 0.25*x1
// ds(t)/dt = 2*t*x2 + (2-2t)*cx + (2t-2)*x1
// ds(0) = 2*(cx-x1)

static void draw_arc(gfxdrawer_t*draw, double x, double y, double a1, double a2, double r)
{
    if(a2<a1) a2+=M_PI*2;

    double d = (a2-a1);
    int steps = ceil(8*d/(M_PI*2)); // we use 8 splines for a full circle
    if(!steps) return;

    int t;
    double step = (a2-a1)/steps;
    double lastx = x+cos(a1)*r;
    double lasty = y+sin(a1)*r;

    /* we could probably build a table for this- there are only 8
       possible values for step */
    double r2 = r*(2-sqrt(0.5+0.5*cos(step)));

    for(t=1;t<=steps;t++) {
	double a = a1 + t*step;
	double c = cos(a)*r;
	double s = sin(a)*r;
	double xx = c + x;
	double yy = s + y;
	//double dx = (s*step/2 + lastx);
	//double dy = (-c*step/2 + lasty);
	double dx = x + cos(a-step/2)*r2;
	double dy = y + sin(a-step/2)*r2;
	//draw->lineto(draw, xx, yy);
	draw->splineTo(draw, dx, dy, xx, yy);
	lastx = xx;
	lasty = yy;
    }
}

static void draw_single_stroke(gfxpoint_t*p, int num, gfxdrawer_t*draw, double width, gfx_capType cap, gfx_joinType join, double limit)
{
    char do_draw=0;
    leftright_t lastdir = LEFT;
    int start = 0;
    int end = num-1;
    int incr = 1;
    int pos = 0;

    width/=2;
    if(width<=0) 
	width = 0.05;

    /* remove duplicate points */
    int s=1,t;
    for(t=1;t<num;t++) {
	p[s] = p[t];
	if(p[t].x != p[t-1].x || p[t].y != p[t-1].y) {
	    s++;
	} else {
	    num--;
	}
    }

    double alimit = atan(limit / width);

    /* iterate through the points two times: first forward, then backward,
       adding a stroke outline to the right side and line caps after each
       pass */
    int pass;
    for(pass=0;pass<2;pass++) {
	int pos;
	double lastw=0;
	for(pos=start;pos!=end;pos+=incr) {
	    //printf("%d) %.2f %.2f\n", pos, p[pos].x, p[pos].y);
	    double dx = p[pos+incr].x - p[pos].x;
	    double dy = p[pos+incr].y - p[pos].y;
	    double l = sqrt(dx*dx+dy*dy);
	    double w = atan2(dy,dx);
	    if(w<0) w+=M_PI*2;
	    
	    if(pos!=start) {
		double d = w-lastw;
		leftright_t turn;
		if(d>=0 && d<M_PI) turn=LEFT;
		else if(d<0 && d>-M_PI) turn=RIGHT;
		else if(d>=M_PI) {turn=RIGHT;}
		else if(d<=-M_PI) {turn=LEFT;d+=M_PI*2;}
		else {assert(0);}
		if(turn!=LEFT || join==gfx_joinBevel) {
		    /* TODO: does a bevel join extend beyond the segment (i.e.,
		       is it like a square cap or like a butt cap? */
		} else if(join==gfx_joinRound) {
		    draw_arc(draw, p[pos].x, p[pos].y, lastw-M_PI/2, w-M_PI/2, width);
		} else if(join==gfx_joinMiter) {
		    if(d/2<alimit) {
			double r2 = width*(1-sin(d/2)+tan(d/2));
			double addx = cos(lastw-M_PI/2+d/2)*r2;
			double addy = sin(lastw-M_PI/2+d/2)*r2;
			draw->lineTo(draw, p[pos].x+addx, p[pos].y+addy);
		    } else {
			/* convert to bevel join, which always looks the same (is
			   independent of miterLimit TODO: verify this */
		    }
		}
	    }

	    double addx = cos(w-M_PI/2)*width;
	    double addy = sin(w-M_PI/2)*width;
	    draw->lineTo(draw, p[pos].x+addx, p[pos].y+addy);
	    //printf("-- %.2f %.2f (angle:%d)\n", px1, py1, (int)(180*w/M_PI));
	    double px2 = p[pos+incr].x + addx;
	    double py2 = p[pos+incr].y + addy;
	    //printf("-- %.2f %.2f (angle:%d)\n", px2, py2, (int)(180*w/M_PI));
	    draw->lineTo(draw, p[pos+incr].x+addx, p[pos+incr].y+addy);

	    lastw = w;
	}
	/* draw stroke ends */
	if(cap == gfx_capButt) {
	    double c = cos(lastw-M_PI/2)*width;
	    double s = sin(lastw-M_PI/2)*width;
	    draw->lineTo(draw, p[pos].x-c, p[pos].y-s);
	} else if(cap == gfx_capRound) {
	    draw_arc(draw, p[pos].x, p[pos].y, lastw-M_PI/2, lastw+M_PI/2, width);
	} else if(cap == gfx_capSquare) {
	    double c = cos(lastw-M_PI/2)*width;
	    double s = sin(lastw-M_PI/2)*width;
	    draw->lineTo(draw, p[pos].x+c-s, p[pos].y+s+c);
	    draw->lineTo(draw, p[pos].x-c-s, p[pos].y-s+c);
	    draw->lineTo(draw, p[pos].x-c, p[pos].y-s);
	}
	start=num-1;
	end=0;
	incr=-1;
    }
}

static void draw_stroke(gfxline_t*start, gfxdrawer_t*draw, double width, gfx_capType cap, gfx_joinType join, double miterLimit)
{
    if(!start) 
	return;
    assert(start->type == gfx_moveTo);
    gfxline_t*line = start;
    // measure array size
    int size = 0;
    int pos = 0;
    double lastx,lasty;
    while(line) {
	if(line->type == gfx_moveTo) {
	    if(pos>size) size = pos;
	    pos++;
	} else if(line->type == gfx_lineTo) {
	    pos++;
	} else if(line->type == gfx_splineTo) {
            int parts = (int)(sqrt(fabs(line->x-2*line->sx+lastx) + fabs(line->y-2*line->sy+lasty))*SUBFRACTION);
            if(!parts) parts = 1;
	    pos+=parts+1;
	}
	lastx = line->x;
	lasty = line->y;
	line = line->next;
    }
    if(pos>size) size = pos;
    if(!size) return;

    gfxpoint_t* points = malloc(sizeof(gfxpoint_t)*size);
    line = start;
    pos = 0;
    while(line) {
	if(line->type == gfx_moveTo) {
	    if(pos) draw_single_stroke(points, pos, draw, width, cap, join, miterLimit);
	    pos = 0;
	} else if(line->type == gfx_splineTo) {
            int parts = (int)(sqrt(fabs(line->x-2*line->sx+lastx) + fabs(line->y-2*line->sy+lasty))*SUBFRACTION);
            if(!parts) parts = 1;
	    double stepsize = 1.0/parts;
            int i;
	    for(i=0;i<parts;i++) {
		double t = (double)i*stepsize;
		points[pos].x = (line->x*t*t + 2*line->sx*t*(1-t) + lastx*(1-t)*(1-t));
		points[pos].y = (line->y*t*t + 2*line->sy*t*(1-t) + lasty*(1-t)*(1-t));
		pos++;
	    }
	}
	lastx = points[pos].x = line->x;
	lasty = points[pos].y = line->y;
	pos++;
	line = line->next;
    }
    if(pos) draw_single_stroke(points, pos, draw, width, cap, join, miterLimit);
    free(points);
}

int main()
{
    gfxline_t l[4];
    l[0].type = gfx_moveTo;
    l[0].x = 100;l[0].sx=2;
    l[0].y = 100;l[0].sy=2;
    l[0].next = &l[1];
    l[1].type = gfx_lineTo;
    l[1].x = 100;l[1].sx=2;
    l[1].y = 200;l[1].sy=-2;
    l[1].next = &l[2];
    l[2].type = gfx_lineTo;
    l[2].x = 250;l[2].sx=4;
    l[2].y = 200;l[2].sy=0;
    l[2].next = &l[3];
    l[3].type = gfx_lineTo;
    l[3].x = 200;l[3].sx=0;
    l[3].y = 150;l[3].sy=4;
    l[3].next = 0;


    gfxdevice_t dev;
    gfxdevice_swf_init(&dev);
    dev.setparameter(&dev, "framerate", "25.0");
    int t;
    for(t=0;t<300;t++) {
	dev.startpage(&dev, 700,700);
	gfxline_t*g = l;
	while(g) {
	    g->x += g->sx;
	    g->y += g->sy;
	    if(g->x<200) {g->x=400-g->x;g->sx=-g->sx;}
	    if(g->y<200) {g->y=400-g->y;g->sy=-g->sy;}
	    if(g->x>500) {g->x=1000-g->x;g->sx=-g->sx;}
	    if(g->y>500) {g->y=1000-g->y;g->sy=-g->sy;}
	    g = g->next;
	}
	gfxdrawer_t d;
	gfxdrawer_target_gfxline(&d);
	double width = t/3.0;
	if(width>50) width=100-width;

	draw_stroke(l, &d, width, gfx_capRound, gfx_joinBevel, 500);
	gfxline_t*line = (gfxline_t*)d.result(&d);
	//gfxline_dump(line, stdout, "");

	gfxcolor_t black = {255,0,0,0};
	gfxcolor_t cyan = {255,0,128,128};
	dev.stroke(&dev, l, 2, &black, gfx_capRound, gfx_joinRound, 0);
	dev.stroke(&dev, line, 2, &cyan, gfx_capRound, gfx_joinRound, 0);
	gfxline_free(line);
	dev.endpage(&dev);
    }

    gfxresult_t* result = dev.finish(&dev);
    result->save(result, "test.swf");
    result->destroy(result);
}