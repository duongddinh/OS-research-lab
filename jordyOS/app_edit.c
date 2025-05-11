#include <stdint.h>
#include <stdbool.h>
#include "util.h" 

#define EDIT_BUFFER_SIZE 512 
#define COMMAND_BUFFER_SIZE 64


static char file_content_buffer[EDIT_BUFFER_SIZE];
static int current_content_length = 0;

static char current_filename[MAX_FILENAME_LEN];
static bool file_is_open = false;


static void clear_file_buffer() {
    memset(file_content_buffer, 0, EDIT_BUFFER_SIZE);
    current_content_length = 0;
}

static void clear_current_filename() {
    current_filename[0] = '\0';
    file_is_open = false;
}

static void enter_text_edit_mode() {
    sys_write("--- Text Edit Mode (Press ESC to finish) ---\n");

    file_content_buffer[current_content_length] = '\0';
    if (current_content_length > 0) {
        sys_write(file_content_buffer);
    }

    int line_start_col = 0;
    if (current_content_length > 0) {
        if (file_content_buffer[current_content_length - 1] == '\n') {
            line_start_col = 0;
        } else {
            int last_newline_idx = -1;
            for (int k = current_content_length - 1; k >= 0; k--) {
                if (file_content_buffer[k] == '\n') {
                    last_newline_idx = k;
                    break;
                }
            }
            line_start_col = current_content_length - (last_newline_idx + 1);
        }
    }


    while (current_content_length < EDIT_BUFFER_SIZE -1) { 
        char c = sys_getc(); 

        if (c == 27) { // ASCII 27 is Escape key
            sys_write("\n--- Exiting Text Edit Mode ---\n");
            break; 
        } else if (c == '\n') { 
            if (current_content_length < EDIT_BUFFER_SIZE - 1) {
                file_content_buffer[current_content_length++] = '\n';
                sys_write("\n");
                line_start_col = 0;
            } else {
                 sys_write("\nBuffer full. Cannot add newline.\n");
            }
        } else if (c == 8 || c == 127) { // Backspace or DEL
            if (current_content_length > 0) {
                current_content_length--;
                sys_write("\b \b"); // Erase char on screen
                if (line_start_col > 0) {
                    line_start_col--;
                } else {
                    if (current_content_length > 0) {
                        if (file_content_buffer[current_content_length - 1] == '\n') {
                            line_start_col = 0;
                        } else {
                            int last_newline_idx = -1;
                            for (int k = current_content_length - 1; k >= 0; k--) {
                                if (file_content_buffer[k] == '\n') {
                                    last_newline_idx = k;
                                    break;
                                }
                            }
                            line_start_col = current_content_length - (last_newline_idx + 1);
                        }
                    } else {
                        line_start_col = 0; 
                    }
                }
            }
        } else if (c >= 32 && c < 127) {
            if (current_content_length < EDIT_BUFFER_SIZE - 1) {
                file_content_buffer[current_content_length++] = c;
                char echo[2] = {c, 0};
                sys_write(echo);
                line_start_col++;
                if (line_start_col >= 79) { 
                    //do stuff
                }
            } else {
                    //do stuff

            }
        }
    }
    file_content_buffer[current_content_length] = '\0';

    if (current_content_length >= EDIT_BUFFER_SIZE -1) {
        sys_write("Warning: Edit buffer is full. Some input may have been lost.\n");
    }
}


void app_edit(void) {
    char command_line[COMMAND_BUFFER_SIZE];
    char command[COMMAND_BUFFER_SIZE];
    char argument[COMMAND_BUFFER_SIZE];

    sys_write("Editor v0.4 (In-Memory FS)\n");
    sys_write("Commands: list, new <fn>, open <fn>, edit, save [fn], delete <fn>, quit\n");

    clear_file_buffer();
    clear_current_filename();

    while (1) {
        if (file_is_open) {
            sys_write("edit ["); sys_write(current_filename); sys_write("]# ");
        } else {
            sys_write("edit# ");
        }
        read_line(command_line, COMMAND_BUFFER_SIZE);

        int i = 0, j = 0;
        while (command_line[i] != ' ' && command_line[i] != '\0' && j < COMMAND_BUFFER_SIZE - 1) command[j++] = command_line[i++];
        command[j] = '\0'; 
        while (command_line[i] == ' ') i++;
        j = 0;
        while (command_line[i] != '\0' && j < COMMAND_BUFFER_SIZE - 1) argument[j++] = command_line[i++];
        argument[j] = '\0';

        if (strcmp(command, "quit") == 0) {
            sys_write("Exiting editor...\n"); sys_exit_task(); return; 
        } else if (strcmp(command, "list") == 0) {
            char list_buffer[MAX_FILES * MAX_FILENAME_LEN + MAX_FILES];
            memset(list_buffer, 0, sizeof(list_buffer)); 
            int bytes = sys_list_files(list_buffer, sizeof(list_buffer) -1); 
            if (bytes > 0) {
                list_buffer[bytes] = '\0';
                sys_write("Files:\n");
                sys_write(list_buffer);
                if (bytes > 0 && list_buffer[bytes-1] != '\n') sys_write("\n"); 
            } else if (bytes == 0) sys_write("No files found.\n");
            else sys_write("Error listing files.\n");
        } else if (strcmp(command, "new") == 0) {
            if (argument[0] == '\0') sys_write("Usage: new <filename>\n");
            else {
                clear_file_buffer(); 
                strncpy(current_filename, argument, MAX_FILENAME_LEN -1);
                current_filename[MAX_FILENAME_LEN-1] = '\0'; 
                file_is_open = true;
                current_content_length = 0; 
                sys_write("New file '"); sys_write(current_filename); sys_write("' in buffer. Use 'edit', then 'save'.\n");
            }
        } else if (strcmp(command, "open") == 0) {
            if (argument[0] == '\0') sys_write("Usage: open <filename>\n");
            else {
                clear_file_buffer();
                int bytes_read = sys_read_file(argument, file_content_buffer, EDIT_BUFFER_SIZE -1);
                if (bytes_read >= 0) {
                    current_content_length = bytes_read;
                    file_content_buffer[current_content_length] = '\0';
                    strncpy(current_filename, argument, MAX_FILENAME_LEN -1);
                    current_filename[MAX_FILENAME_LEN-1] = '\0';
                    file_is_open = true;
                    sys_write("File '"); sys_write(current_filename); sys_write("' opened (");
                    char num_buf[12]; itoa(bytes_read, num_buf); sys_write(num_buf);
                    sys_write(" bytes).\nUse 'edit' to modify/view.\n");
                } else if (bytes_read == -1) { sys_write("Error: File '"); sys_write(argument); sys_write("' not found.\n"); clear_current_filename(); }
                else if (bytes_read == -2) { sys_write("Error: Buffer too small for '"); sys_write(argument); sys_write("'. File exceeds editor capacity.\n"); clear_current_filename(); }
                else { sys_write("Error reading file.\n"); clear_current_filename(); }
            }
        } else if (strcmp(command, "edit") == 0) {
            if (!file_is_open && current_filename[0] == '\0') {
                 sys_write("No file. Use 'new <fn>' or 'open <fn>' first.\n");
            } else {
                enter_text_edit_mode();
            }
        } else if (strcmp(command, "save") == 0) {
            const char* filename_to_save = NULL;
            if (argument[0] != '\0') { 
                filename_to_save = argument;
                strncpy(current_filename, argument, MAX_FILENAME_LEN -1);
                current_filename[MAX_FILENAME_LEN-1] = '\0';
                file_is_open = true;
            } else if (file_is_open && current_filename[0] != '\0') {
                filename_to_save = current_filename;
            } else {
                sys_write("Usage: save <filename> (or open/new a file first to save without filename argument)\n");
                continue;
            }

            if (filename_to_save) {
                file_content_buffer[current_content_length] = '\0'; // Ensure null-termination before saving
                int result = sys_write_file(filename_to_save, file_content_buffer, current_content_length);
                if (result == 0) {
                    sys_write("File '"); sys_write(filename_to_save); sys_write("' saved (");
                    char num_buf[12]; itoa(current_content_length, num_buf); sys_write(num_buf);
                    sys_write(" bytes).\n");
                } else {
                    sys_write("Error saving file '"); sys_write(filename_to_save); sys_write("'. Code: ");
                    char num_buf[12]; itoa(result, num_buf); sys_write(num_buf); sys_write(".\n");
                }
            }
        } else if (strcmp(command, "delete") == 0) {
             if (argument[0] == '\0') sys_write("Usage: delete <filename>\n");
             else {
                int result = sys_delete_file(argument);
                if (result == 0) {
                    sys_write("File '"); sys_write(argument); sys_write("' deleted.\n");
                    if (file_is_open && strcmp(current_filename, argument) == 0) {
                        clear_file_buffer(); clear_current_filename(); 
                    }
                } else { sys_write("Error deleting '"); sys_write(argument); sys_write("'. Not found?\n"); }
            }
        } else if (command[0] == '\0') {
        } else {
            sys_write("Unknown command: '");
            sys_write(command);
            sys_write("'.\n");
        }
    }
}
