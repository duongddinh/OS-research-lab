#include "ape_platform.h"
#include "vm/vm.h"
#include "compiler/compiler.h"
#include "../util.h"
#include "programs.h"
#include "../gui.h"
extern char ape_command[96];
static char source[MAX_FILE_SIZE+1];

static bool load_source(const char* name) {
    int size=sys_read_file(name,source,MAX_FILE_SIZE);
    if (size<0) { sys_write("Source file not found. Use ls or edit.\n"); return false; }
    source[size]=0; return true;
}
static void compile_source(const char* name) {
    if (!load_source(name)) return;
    size_t len=strlen(name);
    if (len<4 || strcmp(name+len-4,".ape")) { sys_write("Expected .ape source.\n"); return; }
    char output[MAX_FILENAME_LEN]; strcpy(output,name); strcpy(output+len-4,".apb");
    char* bytes=NULL; size_t size=0; FILE* stream=open_memstream(&bytes,&size);
    if (!stream) exit(1);
    bool ok=compile(source,stream,false); fclose(stream);
    if (ok && sys_write_file(output,bytes,size)==0) printf("Compiled: %s\n",output);
    else sys_write("Compilation failed or RAM file limit exceeded.\n");
    free(bytes);
}
void app_ape(void) {
    ape_heap_reset();
    VM vm; initVM(&vm); vm.instructionLimit=!strcmp(ape_command,"ape snake.ape") ? 50000000 : 5000000;
    if (!strcmp(ape_command,"ape")) {
        char line[2048];
        sys_write("Apelang on jordyOS. Type exit to return to the shell.\n");
        for (;;) {
            sys_write("ape> "); read_line(line,sizeof(line));
            if (!strcmp(line,"exit")) break;
            interpret(&vm,line);
        }
    } else if (!memcmp(ape_command,"ape ",4)) {
        if (load_source(ape_command+4)) executeSource(&vm,source,false);
    } else if (!memcmp(ape_command,"apb ",4)) {
        runBytecode(&vm,ape_command+4);
    } else compile_source(ape_command+8);
    if (vm.graphicsUsed) gui_terminal("JordyOS / Shell");
    freeVM(&vm);
    sys_write("Apelang finished.\n");
}
void ape_seed_files(void) {
    for (size_t i=0;i<sizeof(embedded_programs)/sizeof(embedded_programs[0]);i++)
        sys_write_file(embedded_programs[i].name,embedded_programs[i].source,embedded_programs[i].size);
}
