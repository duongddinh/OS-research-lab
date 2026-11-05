#include "../util.h"
#include "programs.h"
void ape_seed_files(void) {
    for (size_t i=0;i<sizeof(embedded_programs)/sizeof(embedded_programs[0]);i++)
        sys_write_file(embedded_programs[i].name,embedded_programs[i].source,embedded_programs[i].size);
}
