#include <stdint.h>
#include "util.h"
#include "gui.h"

/* BIOS mode 13h: 320x200, one byte per pixel. No hosted graphics library. */
static volatile uint8_t* const pixels=(uint8_t*)0xa0000;
static const uint8_t* font;
static int row, column;
static const char* terminal_title="JordyOS / Shell";
enum { NAVY, PANEL, CARD, MUTED, TEAL, MINT, GOLD, WHITE, INK, SELECTED };
static void outb(uint16_t port,uint8_t value) {
    __asm__ volatile("outb %0,%1" : : "a"(value),"Nd"(port));
}
static void rect(int x,int y,int w,int h,uint8_t color) {
    for (int yy=y;yy<y+h;yy++) for (int xx=x;xx<x+w;xx++) pixels[yy*320+xx]=color;
}
static void glyph(int x,int y,unsigned char c,uint8_t fg,uint8_t bg) {
    if (c>127) c='?';
    for (int yy=0;yy<8;yy++) {
        uint8_t bits=font[c*8+yy];
        for (int xx=0;xx<8;xx++) pixels[(y+yy)*320+x+xx]=(bits&(0x80>>xx))?fg:bg;
    }
}
static void text(int x,int y,const char* label,uint8_t fg,uint8_t bg) {
    while (*label) { glyph(x,y,(unsigned char)*label++,fg,bg);x+=8; }
}
void gui_paint(int x,int y,int w,int h,int color) {
    if (w<=0 || h<=0 || x>=320 || y>=200) return;
    if (x<0) { w+=x; x=0; } if (y<0) { h+=y; y=0; }
    if (x+w>320) w=320-x; if (y+h>200) h=200-y;
    if (w>0 && h>0) rect(x,y,w,h,(uint8_t)color);
}
void gui_drawtext(int x,int y,const char* label,int color) {
    if (y<0 || y>192) return;
    while (*label && x<=312) {
        if (x>=0) {
            unsigned char c=(unsigned char)*label;
            if (c>127) c='?';
            for (int yy=0;yy<8;yy++) for (int xx=0;xx<8;xx++)
                if (font[c*8+yy]&(0x80>>xx)) pixels[(y+yy)*320+x+xx]=(uint8_t)color;
        }
        label++; x+=8;
    }
}

void gui_init(void) {
    font=(const uint8_t*)(*(volatile uint32_t*)0x500);
    static const uint8_t palette[][3]={
        {4,7,13},{8,12,20},{11,16,25},{30,38,45},{10,40,43},
        {24,58,47},{63,49,22},{57,60,63},{3,5,9},{12,27,34}
    };
    outb(0x3c8,0);
    for (unsigned i=0;i<sizeof(palette);i++) outb(0x3c9,((const uint8_t*)palette)[i]);
}
void gui_clear(void) {
    rect(0,0,320,200,INK);rect(0,0,320,19,PANEL);
    rect(0,19,320,1,TEAL);text(8,6,terminal_title,WHITE,PANEL);
    rect(0,191,320,9,PANEL);text(8,192,"JordyOS  /  Apelang runtime",MUTED,PANEL);
    row=column=0;
}
void gui_terminal(const char* title) { terminal_title=title;gui_clear(); }
void gui_putc(char c) {
    rect(8+column*8,24+row*8,8,8,INK);
    if (c=='\b') { if (column>0) column--;rect(8+column*8,24+row*8,8,8,INK); }
    else if (c=='\n') { column=0;row++; }
    else if (c=='\r') column=0;
    else {
        glyph(8+column*8,24+row*8,(unsigned char)c,WHITE,INK);
        if (++column>=38) { column=0;row++; }
    }
    if (row>=20) {
        for (int y=24;y<176;y++) for (int x=8;x<312;x++) pixels[y*320+x]=pixels[(y+8)*320+x];
        rect(8,176,304,8,INK);row=19;
    }
    rect(8+column*8,30+row*8,7,1,MINT);
}
static void icon(int x,int y,int index,uint8_t bg) {
    if (index==0) {
        rect(x,y,25,24,MINT);rect(x+3,y+3,19,5,INK);
        for (int a=0;a<3;a++) for (int b=0;b<2;b++) rect(x+4+a*6,y+12+b*6,3,3,TEAL);
    } else if (index==1) {
        rect(x+3,y,19,24,GOLD);rect(x+7,y+5,11,2,bg);
        rect(x+7,y+10,11,2,bg);rect(x+7,y+15,8,2,bg);
    } else if (index==2) {
        rect(x,y+2,26,20,TEAL);text(x+1,y+8,">_",MINT,TEAL);
    } else {
        rect(x,y+2,26,20,MUTED);rect(x+2,y+4,22,16,INK);text(x+4,y+8,">",WHITE,INK);
    }
}
static void desktop(int selected) {
    rect(0,0,320,200,NAVY);rect(0,0,320,25,PANEL);
    rect(10,8,7,7,MINT);text(25,8,"jordyOS",WHITE,PANEL);
    text(218,8,"Research OS",MUTED,PANEL);
    static const char* labels[]={"Calc","Editor","Apelang","Shell","Snake","History"};
    static const char* files[]={"calc.ape","edit.ape","REPL","Built-in","snake.ape","history.ape"};
    for (int i=0;i<6;i++) {
        int x=14+(i%3)*100,y=45+(i/3)*64;
        uint8_t bg=i==selected?SELECTED:CARD;
        rect(x,y,92,62,bg);rect(x,y,3,62,i==selected?MINT:CARD);
        if (i==4) {
            rect(x+12,y+10,22,7,MINT);rect(x+27,y+10,7,20,MINT);
            rect(x+12,y+23,20,7,MINT);rect(x+14,y+12,2,2,INK);
            rect(x+42,y+11,7,7,GOLD);
        } else if (i==5) {
            rect(x+12,y+7,22,24,GOLD);
            rect(x+17,y+12,12,2,INK);rect(x+17,y+18,12,2,INK);rect(x+17,y+24,8,2,INK);
        } else icon(x+10,y+6,i,bg);
        text(x+8,y+35,labels[i],WHITE,bg);
        text(x+(i==5?3:8),y+49,files[i],i==selected?MINT:MUTED,bg);
        glyph(x+77,y+6,'1'+i,MUTED,bg);
    }
    rect(0,180,320,20,PANEL);text(8,186,"Tab / arrows: select   Enter: open",MUTED,PANEL);
}
int gui_desktop(void) {
    int selected=0;desktop(selected);
    sys_write("\nDesktop: 1 Calculator, 2 Editor, 3 Apelang, 4 Shell, 5 Snake, 6 History\ndesktop> ");
    /* sys_write also paints the console; redraw the desktop after serial announcement. */
    desktop(selected);
    for (;;) {
        char c=sys_getc();
        if (c>='1'&&c<='6') return c-'1';
        if (c=='\n') return selected;
        if (c==27) return 3;
        if (c=='\t'||c==0x12) selected=(selected+1)%6;
        else if (c==0x11) selected=(selected+5)%6;
        else if (c==0x13||c==0x14) selected=selected<3 ? selected+3 : selected-3;
        else continue;
        desktop(selected);
    }
}
