#ifndef JORDY_GUI_H
#define JORDY_GUI_H
void gui_paint(int x,int y,int w,int h,int color);
void gui_drawtext(int x,int y,const char* text,int color);
void gui_init(void);
void gui_terminal(const char* title);
void gui_putc(char c);
void gui_clear(void);
int gui_desktop(void);
#endif
