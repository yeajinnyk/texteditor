/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>


/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8


enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN

};

// the rest are automatically incremented as list goes on


/*** data ***/

typedef struct erow {		// "editor row". stores a line of text as a pointer to the dynamically allocated character data and a length
	int size;
	int rsize;
	char *chars;
	char *render;
} erow;

struct editorConfig {
	int cx, cy; 	// E.cx is the horizontal coord of the cursor (col) and E.cy is the vertical coord of the cursor (row)
	int rx;
	int rowoff;	// keeps track of what row of the file the user is currently scrolled to
	int coloff;
	int screenrows;
	int screencols;

	int numrows;
	erow *row;

	char *filename;	

	char statusmsg[80];
	time_t statusmsg_time;

	struct termios orig_termios;
};

struct editorConfig E;

/*** prototypes ***/
void editorSetStatusMessage(const char *fmt, ...);

/*** terminal ***/


void die(const char *s) { 	// error handling
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}


void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}


void enableRawMode() {

	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); 	 // read current attributes into a struct
	atexit(disableRawMode); // atexit causes disableRawMode() to be called automatically when program exits.

	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);			// disable ctrl s and ctrl q
	raw.c_oflag &= ~(OPOST);			// turn off all outpost processing
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // turn off canonical mode (read input byte by bite, not line by line)
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
	// TCSAFLUSH specifies when to apply the change

}  


// waits for one keypress and returns it.
int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == '\x1b') {
		char seq[3];
		
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

		if (seq[0] == '[') {

			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				}
			}
			else {

				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
				}
			}
		}
		else if (seq[0] == '0') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}


		return '\x1b';
	}
	else {
		return c;
	}
}

int getCursorPosition(int *rows, int *cols) {

	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}

	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int *cols) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;	// move cursor right (C) and down (B)
		return getCursorPosition(rows, cols);
	}
	else {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
		return 0;
	}
}


/*** row operations ***/

// converts chars index into a render index
int editorRowCxToRx(erow *row, int cx) {
	int rx = 0;
	int j;
	for (j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}
	

void editorUpdateRow(erow *row) {
	int tabs = 0;
	int j;
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') tabs++;
	}


	free(row->render);
	row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);
;
	int idx = 0;
	
	for (j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
		}
		else {
			row->render[idx++] = row->chars[j];
		}
	}

	row->render[idx] = '\0';
	row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
	E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

	int at = E.numrows;
	E.row[at].size = len;
	E.row[at].chars = malloc(len + 1);
	memcpy(E.row[at].chars, s, len);
	E.row[at].chars[len] = '\0';

	E.row[at].rsize = 0;
	E.row[at].render = NULL;
	editorUpdateRow(&E.row[at]);

	E.numrows++;
}

// writes a single character into an erow at a given position
void editorRowInsertChar(erow *row, int at, int c) {
	if (at < 0 || at > row->size) at = row->size;
	
	row->chars = realloc(row->chars, row->size + 2);

	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);

	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
}


/*** editor operations ***/

// inserts char into the position that the cursor is at
void editorInsertChar(int c) {
	if (E.cy == E.numrows) {
		editorAppendRow("", 0);
	}
	editorRowInsertChar(&E.row[E.cy], E.cx, c);
	E.cx++;
}


/*** file i/o ***/

// converts array of erow structs into a single string that is ready to be written out to a file
char *editorRowsToString(int *buflen) {
	int totlen = 0;
	int j;

	for (j = 0; j < E.numrows; j++) {
		totlen += E.row[j].size + 1;
	}
	*buflen = totlen;

	char *buf = malloc(totlen);
	char *p = buf;

	for (j = 0; j < E.numrows; j++) {
		memcpy(p, E.row[j].chars, E.row[j].size);
		p += E.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

// takes a filename and opens the file
void editorOpen(char *filename) {
	free(E.filename);
	E.filename = strdup(filename);	


	// open an actual file
	FILE *fp = fopen(filename, "r");
	if (!fp) die("fopen");

	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;

		editorAppendRow(line, linelen);;
	}

	free(line);
	fclose(fp);
	
} 

void editorSave() {
	if (E.filename == NULL) return;

	int len;
	char *buf = editorRowsToString(&len);

	
	int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);

				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Can't save! I/o error: %s", strerror(errno));
}


/*** append buffer ***/

struct abuf {
	char *b;
	int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
	char *new = realloc(ab->b, ab->len + len);

	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf *ab) {
	free(ab->b);
}


/*** output ***/


void editorScroll() {
	E.rx = 0;
	if (E.cy < E.numrows) {
		E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
	}
	
	// vertical
	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}

	// horizontal
	if (E.rx < E.coloff) {
		E.coloff = E.rx;
	}
	if (E.rx >= E.coloff + E.screencols) {
		E.coloff = E.rx - E.screencols + 1;
	}
}

void editorDrawRows(struct abuf *ab) {
	int y;
	for (y = 0; y < E.screenrows; y++) {

		int filerow = y + E.rowoff;
		if (filerow >= E.numrows) {

			// welcome message only displays if text buffer is empty
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > E.screencols) welcomelen = E.screencols;
			
				// for centering
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					abAppend(ab, "~", 1);
					padding--;
				}

				while (padding --) abAppend(ab, " ", 1);

				abAppend(ab, welcome, welcomelen);
			}
			else {
				abAppend(ab, "~", 1);
			}

		}
		else {
			int len = E.row[filerow].rsize - E.coloff;
			if (len < 0) len = 0;
			if (len > E.screencols) len = E.screencols;
			abAppend(ab, &E.row[filerow].render[E.coloff], len);
		}

		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);

	}
}

// displays a status bar 
void editorDrawStatusBar(struct abuf *ab) {
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];

	int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);
	int rlen = snprintf(rstatus, sizeof(status), "%d/%d", E.cy + 1, E.numrows);	// shows current line number

	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);

	while (len < E.screencols) {

		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		}
		else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5) abAppend(ab, E.statusmsg, msglen);

}


void editorRefreshScreen() {
	editorScroll();

	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	// the first byte  "\x1b" is the escape character (27 in decimal)
	abAppend(&ab, "\x1b[H", 3);										// other 3 bytes is "[2J"
							// escape sequence always starts with escape character (27) followed by [.
							// J command (Erase In Display) to clear screen. 2 says clear entire screen
							// H command positions cursor
	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) +  1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6); // hiding and showing cursor
	
	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);

}

// takes in a format string and a variable number of arguments
void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}


/*** input ***/


// use WASD to move the cursor
void editorMoveCursor(int key) {

	erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

	switch (key) {
		case ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			}
			else if (E.cy > 0) {	// allows user to press <- at the beginning of the line to move to the end of the prev line
				E.cy--;
				E.cx = E.row[E.cy].size;
			}
			break;
		case ARROW_RIGHT:
			if (row && E.cx < row->size) {
				E.cx++;
			}
			else if (row && E.cx == row->size) {	// allows user to press -> at end of a line to go to the beginning of the next line
				E.cy++;
				E.cx = 0;
			}
			break;
		case ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case ARROW_DOWN:
			if (E.cy < E.numrows) {
				E.cy++;
			}
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}


// waits for a keypress and then handles it
void editorProcessKeypress() {
	int c = editorReadKey();

	switch (c) {
		case '\r':		// ENTER key
			/* TODO */
			break;

		case CTRL_KEY('q'):

			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);		

			exit(0);
			break;
		
		case CTRL_KEY('s'):
			editorSave();
			break;

		case HOME_KEY:
			E.cx = 0;
			break;

		case END_KEY:
			if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
	
			break;
		
		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			/* TODO */
			break;

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP) {
					E.cy = E.rowoff;
				}
				else if (c == PAGE_DOWN) {
					E.cy = E.rowoff + E.screenrows - 1;
					if (E.cy > E.numrows) E.cy = E.numrows;
				}

				int times = E.screenrows;
				while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
			editorMoveCursor(c);
			break;
		
		case CTRL_KEY('l'):
		case '\x1b':
			break;
	

		default:		// any keypress that isn't mapped to another editor function is inserted directly
			editorInsertChar(c);
			break;

	}
}


/*** init ***/

void initEditor() { 	// initializes all the fields in the E struct
	E.cx = 0;
	E.cy = 0;
	E.rx = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.row = NULL;
	E.filename = NULL;

	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");

	E.screenrows -= 2;
}

int main(int argc, char *argv[]) {

	enableRawMode();
	initEditor();
	
	if (argc >= 2) {
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("HELP: Ctrl-S = save |  Ctrl-Q = quit");

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
