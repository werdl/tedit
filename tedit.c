#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include "appbuf.c"


#define TEDIT_V "0.1.0"
typedef struct {
    int size;
    char * chars;
} EditorRow;
struct GlobalConfig {
    int cx;
    int cy;
    int RowOffset;
    int screenrows;
    int screencols;
    struct termios _orig;
    int numrows;
    EditorRow *row;
};

struct GlobalConfig editor;
void TildeColumn(struct AppendBuffer * ab);

#define ctrl(k) ((k) & 0x1f)
void ScrollScreen(void) {
    if (editor.cy<editor.RowOffset) editor.RowOffset=editor.cy;
    if (editor.cy>editor.RowOffset+editor.screenrows) editor.RowOffset=editor.cy-editor.screenrows-1;
}
void refresh() {
    ScrollScreen();
    struct AppendBuffer _ab = AB_INIT;
    AppendAB(&_ab, "\x1b[?25l", 6);
    AppendAB(&_ab,"\x1b[H", 3);

    TildeColumn(&_ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (editor.cy-editor.RowOffset) + 1, editor.cx + 1);
    AppendAB(&_ab, buf, strlen(buf));
    AppendAB(&_ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, _ab.b, _ab.len);
    FreeAB(&_ab);
}
// VT100

void die(const char * msg) {
    refresh();
    perror(msg);
    exit(1);
}
void NormalMode(void) {
    if (tcsetattr(STDIN_FILENO,TCSAFLUSH,&editor._orig)==-1) die("tcsetattr");
}
void RawMode(void) {
    if (tcgetattr(STDIN_FILENO, &editor._orig)==-1) die("tcgetattr");
    atexit(NormalMode);
    struct termios raw = editor._orig;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8); // bitmask
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);


    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    // raw mode flags
    if (tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw)==-1) die("tcsetattr");
}
enum SpecialKeys {
    ARROW_LEFT = 4242, // H2G2 (value is not important, just out of char type range)
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DELETE_KEY
};
int ReadKey(void) {
    int retcode;
    char cur;
    while ((retcode = read(STDIN_FILENO, &cur, 1)) != 1) {
        if (retcode == -1 && errno != EAGAIN) die("read");
    }
    if (cur == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                    if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DELETE_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    } 
    return cur;
}
void MoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (editor.cx!=0) editor.cx--;
            break;
        case ARROW_RIGHT:
            if (editor.cx!=editor.screencols-1) editor.cx++;
            break;
        case ARROW_UP:
            if (editor.cy!=0) editor.cy--;
            break;
        case ARROW_DOWN:
            if (editor.cy<editor.numrows+1) editor.cy++;
            break;
    }
}
void ProcessKey() {
    int cur=ReadKey();

    switch (cur) {
        case ctrl('q'):
            refresh();
            exit(0);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            MoveCursor(cur);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int screencopy=editor.screenrows;
                while (screencopy--) MoveCursor(cur==PAGE_UP?ARROW_UP:ARROW_DOWN);
            }
        case HOME_KEY:
            editor.cx=0;
            break;
        case END_KEY:
            editor.cx=editor.screencols-1;
            break;
    }
}
int CursorPosition(int * rows, int * cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    ReadKey();
    return -1;
}
int WinSize(int * rows, int * columns) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // fallback
        ReadKey();
        return CursorPosition(rows,columns);
    } else {
        *columns = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void TildeColumn(struct AppendBuffer * ab) {
    for (int y=0; y<editor.screenrows;y++) {
        int filerow=y+editor.RowOffset;
        if (filerow>editor.numrows) {
            if (editor.numrows==0 && y == editor.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Tedit TExt EDITor -- version %s", TEDIT_V);
                int padding = (editor.screencols - welcomelen) / 2;
                if (padding) {
                    AppendAB(ab, "~", 1);
                    padding--;
                }
                while (padding--) AppendAB(ab, " ", 1);
                AppendAB(ab,welcome,welcomelen);
            } else {
                AppendAB(ab, "~", 1);
            }
        } else {
            int len=editor.row[filerow].size;
            if (len>editor.screencols) len=editor.screencols;
            AppendAB(ab,editor.row[filerow].chars,len);
        }
        AppendAB(ab, "\x1b[K", 3);
        if (y < editor.screenrows - 1) {
            AppendAB(ab, "\r\n", 2);
        }
    }
}
void NewRow(char * s,size_t len) {
    editor.row=realloc((void *)editor.row,sizeof(EditorRow)*(editor.numrows+1));

    int at = editor.numrows;
    editor.row[at].size=len;
    editor.row[at].chars=malloc(len + 1);
    memcpy(editor.row[at].chars,s,len);
    editor.row[at].chars[len]='\0';
    editor.numrows++;
}
void OpenFile(char * filename) {
    FILE * fp=fopen(filename,"r");

    if (!fp) die("fopen");
    char * line=NULL;
    size_t linecap=0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        NewRow(line, linelen);
    }
    free(line);
    fclose(fp);
}
void init(void) {
    editor.cx=0;
    editor.cy=0;
    editor.RowOffset=0;
    editor.numrows=0;
    editor.row=NULL;
    if (WinSize(&editor.screenrows,&editor.screencols)==-1) die("WinSize");
}
int main(int argc, char ** argv) {
    RawMode();
    init();
    if (argc>=2) {
        OpenFile(argv[1]);
    }

    while (true) {
        refresh();
        ProcessKey();
    }
    return 0;
}