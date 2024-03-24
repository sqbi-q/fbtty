#define STB_IMAGE_IMPLEMENTATION
#include "libs/stb_image.h" // includes <stdio.h>
#include <fcntl.h> // open
#include <unistd.h> // close, pipe, fork, dup, execl, _exit
#include <getopt.h>
#include <sys/ioctl.h> // ioctl
#include <linux/fb.h> // ioctl requests
#include <sys/mman.h> // mmap, munmap
#include <errno.h>
#include <sys/wait.h> // wait
#include <termios.h> // for flushing typeahead

#define RESULT_SIZE 32

const char usage_note[] = 
    "Usage: fbtty [options] [-o <out_path>] <img_path>\n"
    "Write image from <img_path> to /dev/fb0 or other path if <out_path> provided.\n"
    "\n"
    "Options:\n"
    "  -h --help                          Print this note.\n"
    "  -o <out_path> --output=<out_path>  Write bytes of image to device with <out_path> path.\n"
    "                                     Defaults to /dev/fb0\n"
    "Cursor options:\n"
    "  -b --bottom                        Put cursor at the end of image. Default behaviour.\n"
    "  -f --flow                          Put cursor to the right of image.\n"
    "  -t --top                           Put cursor at the begin of image.\n";


int call_terminal_oper(char cmd_result[RESULT_SIZE], const char* command, int argc, char* const args[]) {
    FILE *cmd_output;
    int pipefd[2];

    const char script_path[] = "./libs/terminal_oper.sh";
    char script_realpath[PATH_MAX];
    if (realpath(script_path, script_realpath) == NULL){
        fprintf(stderr, "Error: bash script not found\n");
        fprintf(stderr, "realpath: %s\n", strerror(errno));
        return 1;
    }

    // initalize argv as `terminal.oper.sh <command> [args...]`
    int i = 0;
    const char* argv[2+argc+1];
    
    argv[i++] = "terminal_oper.sh";
    argv[i++] = command;
    for (int j=0; i < 2+argc; i++, j++) {
        argv[i] = args[j];
    }
    argv[i] = NULL;

    if (pipe(pipefd) == -1) {
        fprintf(stderr, "Error: pipes couldn't be created\n");
        return 1;
    }
   
    int result = fork();
    switch(result) {
        case -1:
            fprintf(stderr, "Error: fork failure\n");
            return 1;
        case 0:
            // child process
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            close(pipefd[1]);
            //exec here!
            execv(script_realpath, (char* const*)argv);
            fprintf(stderr, "Error: %s\n", strerror(errno));
            _exit(1);
    }
    // parent process
    close(pipefd[1]); // no input
    cmd_output = fdopen(pipefd[0], "r");

    fgets(cmd_result, RESULT_SIZE, cmd_output);

    int status;
    wait(&status);
    
    if (WEXITSTATUS(status) != 0) {
        fprintf(stderr, "Error: child process exited with error\n");
        return 1;
    }

    return 0;
}

// Uses `terminal_oper get_tty_cellsize`
void get_cell_size(int *size) {
    char cmd_result[RESULT_SIZE];
    call_terminal_oper(cmd_result, "get_tty_cellsize", 0, NULL);
    sscanf(cmd_result, "%d%d", &size[0], &size[1]);
}

// Uses `terminal_oper get_cursor`
void get_cursor_pos(const int* margin, int *position) {
    char cmd_result[RESULT_SIZE];
    call_terminal_oper(cmd_result, "get_cursor", 0, NULL);
    sscanf(cmd_result, "%d%d", &position[0], &position[1]);
    position[0] += margin[0];
    position[1] += margin[1];
}

// Set position_px to position adjusted by tty offset
// Uses `terminal_oper get_tty_offset`
void get_cursor_pos_px(const int *cursor_pos, const int *cell_size, int *position_px) {
    char cmd_result[RESULT_SIZE];
    int offset[2];
    call_terminal_oper(cmd_result, "get_tty_offset", 0, NULL);
    sscanf(cmd_result, "%d%d", &offset[0], &offset[1]);
    position_px[0] = (cursor_pos[0]+offset[0])*cell_size[0];
    position_px[1] = (cursor_pos[1]+offset[1])*cell_size[1];
}

// Uses `terminal_oper set_cursor <col> <line>`
void set_cursor_pos(const int *position) {
    char cmd_result[RESULT_SIZE];
    char args[2][RESULT_SIZE];
    sprintf(args[0], "%d", position[0]);
    sprintf(args[1], "%d", position[1]);
    
    call_terminal_oper(cmd_result, "set_cursor", 2, (char* const[]){args[0], args[1]});
    printf("%s", cmd_result);
}


#define RED(pixel) *(pixel)
#define GRN(pixel) *(pixel+1)
#define BLE(pixel) *(pixel+2)
#define ALPHA(pixel) *(pixel+3)

void write_image(int* offset, int width, int height, int img_line_length, int fb_line_length, unsigned char* data, char* fb_ptr) {
    for (int y=0; y < height; y++) {
        for (int x=0; x < width; x++) {
            int fb_loc = (x + offset[0]) * 4 + (y + offset[1]) * fb_line_length;
            int data_loc = x * 3 + y * img_line_length;
            RED(fb_ptr + fb_loc) = BLE(data + data_loc);
            GRN(fb_ptr + fb_loc) = GRN(data + data_loc);
            BLE(fb_ptr + fb_loc) = RED(data + data_loc);
            ALPHA(fb_ptr + fb_loc) = 0;
        }
    }
}


typedef struct {
    int begin_pos[2];
    int begin_pos_px[2];
    int end_pos[2];
} cursor;

typedef enum {
    END_AT_TOP,     // set cursor on prompt line
    END_AT_BOTTOM,  // set cursor at line after image
    FLOW_AROUND     // set cursor to the right of image
} cursor_mode; 

/**
 * Assign position to cursor.
 * *mode* decides where cursor ends up after writing image.
 */
void init_cursor(cursor *cur, cursor_mode mode, int indent, int* cell_size, int image_lines, int image_cols) {
    // top-left position starts from (1, 1) but (0, 0) is needed
    int margin[] = {-1, -1};
    get_cursor_pos(margin, cur->begin_pos);
    cur->begin_pos[0] += indent;
    get_cursor_pos_px(cur->begin_pos, cell_size, cur->begin_pos_px); 
  
    get_cursor_pos(margin, cur->end_pos);
    
    if (mode == END_AT_TOP) {
        cur->end_pos[1] -= 1;
    }
    else if (mode == END_AT_BOTTOM) {
        cur->end_pos[0] = 0;
        cur->end_pos[1] += image_lines;
    }
    else if (mode == FLOW_AROUND) {
        cur->end_pos[0] += 1+indent+image_cols;
    }
}


typedef struct {
    int line_length;        // length of line in bytes 
    long screen_size;       // size of screen in bytes, used in mmap
    int terminal_size[2];   // size of terminal (or pane) in columns and lines
} term_info;

/**
 * Assign information about terminal.
 * *fbfd* - file descriptor of framebuffer device.
 */
void init_term_info(int fbfd, term_info *info) {
    struct fb_fix_screeninfo finfo;
    ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
   
    struct fb_var_screeninfo vinfo;
    ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);

    struct winsize winfo;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &winfo);

    info->screen_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
    info->line_length = finfo.line_length;
    info->terminal_size[0] = winfo.ws_col;
    info->terminal_size[1] = winfo.ws_row;
}


int main(int argc, char *argv[]) {
    // handle arguments
    const char *img_path = NULL;
    const char *out_path = "/dev/fb0";
    cursor_mode mode = END_AT_BOTTOM;
  
    const char *optstring = ":ho:bft";
    struct option options[] = {
        {"help",    0, NULL, 'h'},
        {"output",  1, NULL, 'o'},
        {"bottom",  0, NULL, 'b'},
        {"flow",    0, NULL, 'f'},
        {"top",     0, NULL, 't'},
        {0, 0, 0, 0}
    };

    int opt_ret = -1;
    while((opt_ret = getopt_long(argc, argv, optstring, options, NULL)) != -1) {
        switch(opt_ret) {
            case 'h':
                printf(usage_note);
                exit(0);
                break;
            case 'o':
                out_path = optarg;
                break;
            case 'b': 
                mode = END_AT_BOTTOM;
                break;
            case 'f':
                mode = FLOW_AROUND;
                break;
            case 't':
                mode = END_AT_TOP;
                break;
        
            case ':':
                fprintf(stderr, "Error: Argument for option '%c' not found.\n", optopt);
                exit(1);
            case '?':
                fprintf(stderr, "Error: Unknown option '%c'.\n", optopt);
                exit(1);
        }  
    }

    // get input image path
    if (argc <= optind) {
        fprintf(stderr, "Error: Image path was not provided.\n");
        fprintf(stderr, usage_note);
        return 1;
    }
    img_path = argv[optind];
    //

    // load image and framebuffer
    int width, height, channels;
    unsigned char *data = stbi_load(img_path, &width, &height, &channels, 3);

    if (data == NULL) {
        fprintf(stderr, "Error: image %s couldn't be loaded: ", img_path);
        fprintf(stderr, "%s\n", stbi_failure_reason());
        return 1;
    }

    int fbfd = open(out_path, O_RDWR);
   
    if (fbfd == -1) {
        fprintf(stderr, "Error: output device %s not found\n", out_path);
        stbi_image_free(data);
        return 1;
    }

    term_info tinfo; init_term_info(fbfd, &tinfo);

    char* fb_ptr = (char*) mmap(0, tinfo.screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);    
    
    if ((long) fb_ptr == -1) {
        fprintf(stderr, "Error: failed to map framebuffer\n");
        fprintf(stderr, "mmap: %s\n", strerror(errno));
        stbi_image_free(data);
        close(fbfd);
        return 1;
    }
    //


    int cell_size[2];
    get_cell_size(cell_size);

    int image_lines = ceil((double) height / cell_size[1]);
    int image_cols = ceil((double) width / cell_size[0]);

    int indent = 1;

    cursor cursor;
    tcflush(STDIN_FILENO, TCIOFLUSH);
    init_cursor(&cursor, mode, indent, cell_size, image_lines, image_cols);

    int image_end_pos[2];
    int img_line_length = width * 3;

    // calculate height exceed
    image_end_pos[1] = cursor.begin_pos[1] + image_lines;
    int height_exceed = image_end_pos[1] - tinfo.terminal_size[1]; 
    if (height_exceed > 0) 
        height -= (height_exceed+1)*cell_size[1];
    
    // calculate width exceed
    image_end_pos[0] = cursor.begin_pos[0] + indent + image_cols;
    int width_exceed = image_end_pos[0] - tinfo.terminal_size[0];
    if (width_exceed > 0) {
        width -= width_exceed*cell_size[0];
    }

    // TODO fix image being overwritten by character created by cursor after newline
    write_image(cursor.begin_pos_px, width, height, img_line_length, tinfo.line_length, data, fb_ptr);
    
    int image_bottom_pos = fmin(image_end_pos[1], tinfo.terminal_size[1]-2);
    set_cursor_pos((int[]){0, image_bottom_pos});
    
    if (height_exceed > 0 && width_exceed > 0)
        printf("%d line(s) and %d column(s) exceed", height_exceed, width_exceed);
    else if (height_exceed > 0)     printf("%d line(s) exceed", height_exceed);
    else if (width_exceed > 0)      printf("%d column(s) exceed", width_exceed); 
    
    set_cursor_pos(cursor.end_pos);

    munmap(fb_ptr, tinfo.screen_size);
    close(fbfd);
    stbi_image_free(data);

    return 0;
}
