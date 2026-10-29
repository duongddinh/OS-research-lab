#include "ape_platform.h"
#include "../util.h"
#include "../gui.h"

void ape_paint(int x,int y,int w,int h,int color) { gui_paint(x,y,w,h,color); }
void ape_drawtext(int x,int y,const char* text,int color) { gui_drawtext(x,y,text,color); }
const char* ape_key(void) {
    static char text[2]; char c=sys_pollc();
    if (c==0x11) return "left"; if (c==0x12) return "right";
    if (c==0x13) return "up"; if (c==0x14) return "down";
    if (c==27) return "escape"; if (c=='\n') return "enter";
    text[0]=c; text[1]=0; return text;
}
static void port_write(uint16_t port,uint8_t value) {
    __asm__ volatile("outb %0,%1" : : "a"(value),"Nd"(port));
}
static uint8_t port_read(uint16_t port) {
    uint8_t value; __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));return value;
}
void ape_wait(unsigned ms) {
    // PIT channel 2, one-shot mode. Interrupts and host timing are unnecessary.
    uint8_t gate=port_read(0x61);
    while (ms) {
        unsigned step=ms>50?50:ms; ms-=step;
        uint16_t count=(uint16_t)(step*1193);
        port_write(0x61,gate & ~3u); port_write(0x43,0xb0);
        port_write(0x42,count & 255); port_write(0x42,count>>8);
        port_write(0x61,(gate & ~2u)|1);
        while (!(port_read(0x61)&0x20)) __asm__ volatile("pause");
    }
    port_write(0x61,gate);
}

/* Single foreground runtime, 4 MiB arena. Split/coalesce blocks are reused by GC. */
static unsigned char arena[4 * 1024 * 1024] __attribute__((aligned(16)));
typedef struct Block { size_t size; struct Block* next; bool available; } Block;
static Block* first;
void ape_heap_reset(void) {
    first = (Block*)arena;
    *first = (Block){sizeof(arena) - sizeof(Block), NULL, true};
}
void* malloc(size_t size) {
    if (!first) ape_heap_reset();
    if (size > sizeof(arena) - 16) return NULL;
    size = (size + 15) & ~(size_t)15;
    if (!size) size = 16;
    for (Block* b = first; b; b = b->next) {
        if (!b->available || b->size < size) continue;
        if (b->size >= size + sizeof(Block) + 16) {
            Block* tail = (Block*)((char*)(b + 1) + size);
            *tail = (Block){b->size - size - sizeof(Block), b->next, true};
            b->next = tail; b->size = size;
        }
        b->available = false; return b + 1;
    }
    return NULL;
}
void free(void* ptr) {
    if (!ptr) return;
    ((Block*)ptr - 1)->available = true;
    for (Block* b = first; b && b->next;) {
        if (b->available && b->next->available) {
            b->size += sizeof(Block) + b->next->size; b->next = b->next->next;
        } else b = b->next;
    }
}
void* realloc(void* ptr, size_t size) {
    if (!size) { free(ptr); return NULL; }
    if (!ptr) return malloc(size);
    Block* b = (Block*)ptr - 1;
    if (b->size >= size) return ptr;
    void* next = malloc(size);
    if (!next) return NULL;
    memcpy(next, ptr, b->size); free(ptr); return next;
}
void* calloc(size_t count, size_t size) {
    if (count && size > (size_t)-1 / count) return NULL;
    void* p = malloc(count * size);
    if (p) memset(p, 0, count * size);
    return p;
}
void exit(int status) {
    (void)status; sys_write("\nApelang stopped: runtime allocation or I/O failure.\n");
    sys_exit_task(); for (;;) __asm__ volatile("hlt");
}
int memcmp(const void* left, const void* right, size_t size) {
    const unsigned char *a=left, *b=right;
    while (size--) { if (*a != *b) return *a - *b; a++; b++; } return 0;
}
char* strcat(char* d, const char* s) { strcpy(d+strlen(d), s); return d; }
char* strrchr(const char* s, int c) {
    const char* found=NULL;
    do { if (*s == c) found=s; } while (*s++);
    return (char*)found;
}
char* strstr(const char* s, const char* needle) {
    do { size_t i=0; while (needle[i] && s[i] && needle[i]==s[i]) i++;
        if (!needle[i]) return (char*)s;
    } while (*s++);
    return NULL;
}
size_t strcspn(const char* s, const char* reject) {
    size_t n=0;
    for (; s[n]; n++) for (const char* r=reject; *r; r++) if (s[n]==*r) return n;
    return n;
}
int isspace(int c) { return c==' ' || (c>=9 && c<=13); }
int isdigit(int c) { return c>='0' && c<='9'; }
int isalpha(int c) { return (c>='a'&&c<='z') || (c>='A'&&c<='Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
double strtod(const char* s, char** end) {
    const char* initial=s;
    while (isspace(*s)) s++;
    bool neg=*s=='-'; if (*s=='-' || *s=='+') s++;
    bool digits=false; double v=0;
    while (isdigit(*s)) { digits=true; v=v*10+(*s++-'0'); }
    if (*s=='.') {
        s++; double place=0.1;
        while (isdigit(*s)) { digits=true; v+=(*s++-'0')*place; place*=0.1; }
    }
    if (digits && (*s=='e'||*s=='E')) {
        const char* exponent=s++; bool minus=*s=='-';
        if (*s=='-'||*s=='+') s++;
        int power=0;
        if (!isdigit(*s)) s=exponent;
        else { while (isdigit(*s)) { if (power<400) power=power*10+(*s-'0'); s++; }
            if (power>400) power=400;
            while (power--) v=minus?v/10:v*10;
        }
    }
    if (end) *end=(char*)(digits?s:initial);
    return neg?-v:v;
}

char* ape_list_files(void) {
    char* text=malloc(MAX_FILES*(MAX_FILENAME_LEN+1));
    if (!text) return NULL;
    if (sys_list_files(text,MAX_FILES*(MAX_FILENAME_LEN+1))<0) { free(text); return NULL; }
    return text;
}
int remove(const char* path) { return sys_delete_file(path); }

struct ApeFile {
    char* data; size_t size, capacity, pos;
    char** out; size_t* length; int kind; bool writable;
    char name[MAX_FILENAME_LEN];
};
static FILE console_in={.kind=1}, console_out={.kind=2};
FILE *stdin=&console_in, *stdout=&console_out, *stderr=&console_out;
FILE* open_memstream(char** buffer, size_t* length) {
    FILE* f=calloc(1,sizeof(FILE)); if (!f) return NULL;
    f->writable=true; f->out=buffer; f->length=length;
    f->data=calloc(1,1); f->capacity=1;
    if (!f->data) { free(f); return NULL; }
    *buffer=f->data; *length=0; return f;
}
FILE* fopen(const char* path, const char* mode) {
    if (strlen(path)>=MAX_FILENAME_LEN) return NULL;
    FILE* f=calloc(1,sizeof(FILE)); if (!f) return NULL;
    strcpy(f->name,path);
    f->capacity=MAX_FILE_SIZE+1; f->data=malloc(f->capacity);
    if (!f->data) { free(f); return NULL; }
    f->writable=mode[0]=='w';
    if (!f->writable) {
        int size=sys_read_file(path,f->data,MAX_FILE_SIZE);
        if (size<0) { free(f->data); free(f); return NULL; }
        f->size=size;
    }
    f->data[f->size]=0; return f;
}
int fflush(FILE* f) {
    if (f->out) { *f->out=f->data; *f->length=f->size; }
    return 0;
}
int fclose(FILE* f) {
    int status=0; fflush(f);
    if (f->name[0] && f->writable) status=sys_write_file(f->name,f->data,f->size);
    if (!f->out) free(f->data);
    free(f); return status;
}
int fseek(FILE* f, long offset, int origin) {
    long pos=(origin==SEEK_SET?0:origin==SEEK_CUR?(long)f->pos:(long)f->size)+offset;
    if (pos<0 || (size_t)pos>f->size) return -1;
    f->pos=pos; return 0;
}
long ftell(FILE* f) { return f->pos; }
void rewind(FILE* f) { f->pos=0; }
size_t fread(void* ptr, size_t size, size_t count, FILE* f) {
    if (!size) return 0;
    if (count>(f->size-f->pos)/size) count=(f->size-f->pos)/size;
    memcpy(ptr,f->data+f->pos,size*count); f->pos+=size*count; return count;
}
size_t fwrite(const void* ptr, size_t size, size_t count, FILE* f) {
    if (!size || count>((size_t)-1-f->pos-1)/size || !f->writable) return 0;
    size_t n=size*count;
    if (f->pos+n+1>f->capacity) {
        size_t cap=(f->pos+n+1)*2;
        char* data=realloc(f->data,cap); if (!data) return 0;
        f->data=data; f->capacity=cap;
    }
    memcpy(f->data+f->pos,ptr,n); f->pos+=n;
    if (f->pos>f->size) f->size=f->pos;
    f->data[f->size]=0; return count;
}
char* fgets(char* buffer, int size, FILE* f) {
    if (size<2) return NULL;
    if (f->kind==1) { read_line(buffer,size); return buffer; }
    int n=0;
    while (n<size-1 && f->pos<f->size) { char c=f->data[f->pos++]; buffer[n++]=c; if (c=='\n') break; }
    buffer[n]=0; return n?buffer:NULL;
}

/* Formatting for the compiler's diagnostics and VM values; bounded output. */
typedef struct { char* data; size_t capacity, used; } Output;
static void emit(Output* o,char c) { if (o->used+1<o->capacity) o->data[o->used]=c; o->used++; }
static void text(Output* o,const char* s,int precision) { while (*s && precision--) emit(o,*s++); }
static void integer(Output* o,unsigned long n) {
    char buffer[24]; int i=0;
    do { buffer[i++]='0'+n%10; n/=10; } while (n);
    while (i) emit(o,buffer[--i]);
}
static void number(Output* o,double value) {
    union { double d; uint64_t u; } bits={.d=value};
    if ((bits.u>>52 & 2047)==2047) { text(o,(bits.u & 0xfffffffffffffULL)?"nan":(bits.u>>63?"-inf":"inf"),-1); return; }
    if (value<0) { emit(o,'-'); value=-value; }
    if (value==0) { emit(o,'0'); return; }
    int exponent=0;
    while (value>=10) { value/=10; exponent++; }
    while (value<1) { value*=10; exponent--; }
    unsigned digits=(unsigned)(value*100000+0.5);
    if (digits==1000000) { digits=100000; exponent++; }
    char significant[6];
    for (int i=5;i>=0;i--) { significant[i]='0'+digits%10; digits/=10; }
    int last=5; while (last>0 && significant[last]=='0') last--;
    if (exponent < -4 || exponent >= 6) {
        emit(o,significant[0]);
        if (last) { emit(o,'.'); for (int i=1;i<=last;i++) emit(o,significant[i]); }
        emit(o,'e'); emit(o,exponent<0?'-':'+');
        unsigned e=exponent<0?-exponent:exponent; if (e<10) emit(o,'0'); integer(o,e);
    } else {
        if (exponent<0) { text(o,"0.",-1); for (int i=-1;i>exponent;i--) emit(o,'0'); }
        for (int i=0;i<=last || i<=exponent;i++) {
            emit(o,i<6?significant[i]:'0');
            if (i==exponent && i<last) emit(o,'.');
        }
    }
}
int vsnprintf(char* buffer,size_t size,const char* format,va_list args) {
    Output o={buffer,size,0};
    while (*format) {
        if (*format!='%') { emit(&o,*format++); continue; }
        format++; int precision=-1;
        if (*format=='.') { format++; if (*format=='*') { precision=va_arg(args,int); format++; } }
        bool wide=*format=='l'||*format=='z'; if (wide) format++;
        switch (*format++) {
            case '%': emit(&o,'%'); break;
            case 's': { const char* s=va_arg(args,const char*); text(&o,s?s:"(null)",precision); break; }
            case 'c': emit(&o,va_arg(args,int)); break;
            case 'd': { long n=wide?va_arg(args,long):va_arg(args,int); unsigned long u=(unsigned long)n; if (n<0) { emit(&o,'-'); u=0-u; } integer(&o,u); break; }
            case 'u': integer(&o,wide?va_arg(args,unsigned long):va_arg(args,unsigned)); break;
            case 'g': number(&o,va_arg(args,double)); break;
            default: emit(&o,'?'); break;
        }
    }
    if (size) buffer[o.used<size?o.used:size-1]=0;
    return o.used;
}
int fprintf(FILE* f,const char* format,...) {
    char buffer[2048]; va_list args; va_start(args,format);
    int n=vsnprintf(buffer,sizeof(buffer),format,args); va_end(args);
    if (f->kind==2) sys_write(buffer); else fwrite(buffer,1,strlen(buffer),f);
    return n;
}
int printf(const char* format,...) {
    char buffer[2048]; va_list args; va_start(args,format);
    int n=vsnprintf(buffer,sizeof(buffer),format,args); va_end(args); sys_write(buffer); return n;
}
int sprintf(char* buffer,const char* format,...) {
    va_list args; va_start(args,format); int n=vsnprintf(buffer,(size_t)-1,format,args); va_end(args); return n;
}
