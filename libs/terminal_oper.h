/* terminal_oper - Operations on terminal
 *
 * Do this:
 *   #define TERMINAL_OPER_IMPLEMENTATION
 * before including this header in one source file.
 */

#ifndef TERMINAL_OPER_H
#define TERMINAL_OPER_H

#include <stdlib.h> // getenv, system
#include <stdio.h>  // popen, fopen, fscanf, sprintf 
#include <string.h> // strcmp
#include <termios.h>
#include <unistd.h> // read, write

// Get size (lines, columns) in chars
void get_cell_size(int* size);

// Get left-top offset (lines, columns) of current terminal (pane)
// Usually 0 0 but in tmux it should be adjusted for pane.
void get_tty_offset(int* offset);

// Get size in pixels
void get_screen_size(int* size);

// Get cursor position relative to pane
void get_cursor_pos(int* position);

// Sets cursor to *pos[0]* column and *pos[1]* line index
// relative to pane (e.g. tmux starts from (0, 0) for each pane)
void set_cursor_pos(const int* position);

#endif

#ifdef TERMINAL_OPER_IMPLEMENTATION
void get_cell_size(int* size) {
    //TODO
    size[0] = 8;
    size[1] = 16;
}

void get_tty_offset(int* offset) {
    if (getenv("TMUX") != NULL) {
        FILE* out = popen("tmux display -p \"#{pane_left} #{pane_top}\"", "r");
        fscanf(out, "%d%d", &offset[0], &offset[1]);
        pclose(out);
    } else {
        offset[0] = 0;
        offset[1] = 0;
    }
}

void get_screen_size(int* size) {
    static const char* fb_size_fp = "/sys/class/graphics/fb0/virtual_size";
    FILE* fb_size_file = fopen(fb_size_fp, "r");
    fscanf(fb_size_file, "%d,%d", &size[0], &size[1]);
    fclose(fb_size_file);
}

void get_cursor_pos(int* position) {
    struct termios tty, oldtty;
    tcgetattr(STDIN_FILENO, &oldtty);
    tcgetattr(STDIN_FILENO, &tty); 
    tty.c_lflag &= ~(ICANON|ECHO); 

    tcsetattr(STDIN_FILENO, TCSANOW, &tty); 
    write(STDOUT_FILENO, "\033[6n", 5);
    
    char code_ret[32] = {0};
    read(STDIN_FILENO, &code_ret, 32);
    sscanf(code_ret, "\033[%d;%dR", &position[1], &position[0]);

    tcsetattr(STDIN_FILENO, TCSANOW, &oldtty);
}

void set_cursor_pos(const int* position) {
    char command[255];
    sprintf(command, "tput cup %d %d", position[1], position[0]);
    system(command);
}

#endif
