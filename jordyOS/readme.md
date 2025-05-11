# jordyOS

`jordyOS` is a minimal, bootable operating system featuring a command-line shell, a calculator application, and a text editor with in-memory file support.

It’s written in **x86 Assembly** and **C**, and is built with an i686-elf cross-compiler toolchain.
The OS runs in **16-bit real mode** initially (via the bootloader), then switches to **32-bit protected mode** where it executes a multitasking kernel.

---

## Features

-   **Bootloader (`bootloader.asm`)**:
    * Sets up the system from a 16-bit real mode environment.
    * Loads the kernel into memory.
    * Transitions the CPU to 32-bit protected mode.
-   **Kernel (`kernel.c`, `kernel_entry.asm`, `ctx_switch.asm`)**:
    * **VGA Text Mode Output**: Displays text on the screen.
    * **Polling Keyboard Input**: Reads keystrokes via I/O ports.
    * **Cooperative Multitasking**: A simple scheduler allows multiple tasks (applications) to run.
    * **System Calls**: Provides an API for:
        * Console I/O (`sys_write`, `sys_getc`).
        * Task management (`sys_yield`, `sys_exit_task`).
        * Screen manipulation (`sys_clear_screen`).
        * In-memory file operations (`sys_list_files`, `sys_read_file`, `sys_write_file`, `sys_delete_file`).
-   **Shell (`sh>`)**:
    * Provides a command-line interface after booting.
    * Parses user input to launch applications or execute built-in commands.
    * **Built-in commands**:
        * `calc`: Launches the calculator application.
        * `edit`: Launches the text editor application.
        * `clear` (or `cls`): Clears the terminal screen.
        * `help`: Displays available shell commands.
-   **Applications**:
    * **`app_calc` (Calculator)**:
        * Interactive command-line interface (`calc>`).
        * Performs basic arithmetic operations: `add`, `sub`, `mul`, `div`.
        * Supports an `exit` or `quit` command to return to the main shell.
    * **`app_edit` (Text Editor)**:
        * Command-driven interface (`edit#` or `edit [filename]#`).
        * **In-Memory File System**:
            * `new <filename>`: Create a new text file in memory.
            * `open <filename>`: Load an existing in-memory file.
            * `save [filename]`: Save the current text buffer to an in-memory file.
            * `list`: List all in-memory files.
            * `delete <filename>`: Remove an in-memory file.
        * **Text Input Mode**: Entered via the `edit` command (within `app_edit`) to add or modify text. Exit with `ESC`.
        * `quit`: Exits the editor and returns to the main shell.

---

## Example Usage

```txt
jordyOS multitask w/ In-Memory FS 

sh> help
Available commands:
  calc    - Run the calculator app
  edit    - Run the text editor app
  clear   - Clear the screen
  help    - Show this help message

sh> calc
Calculator App. Type 'exit' or 'quit' to close.
calc> add 10 5
15
calc> mul 3 7
21
calc> exit
Exiting calculator...

sh> edit
Editor v0.4 (In-Memory FS)
Commands: list, new <fn>, open <fn>, edit, save [fn], delete <fn>, quit
edit# new myfile.txt
New file 'myfile.txt' in buffer. Use 'edit', then 'save'.
edit [myfile.txt]# edit
--- Text Edit Mode (Press ESC to finish) ---
Hello world!
This is a test file.
--- Exiting Text Edit Mode ---
edit [myfile.txt]# save
File 'myfile.txt' saved (36 bytes).
edit [myfile.txt]# list
Files:
myfile.txt
edit [myfile.txt]# quit
Exiting editor...

sh> clear
```
### Install Toolchain (if not already)

```
$ brew install cdrkit
$ brew install cdrtools
$ brew install i686-elf-binutils i686-elf-gcc
$ brew install nasm qemu
```

### Build and Run the OS

```
$ make
$ make run
```
