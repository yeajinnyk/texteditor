/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>


/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f)


/*** data ***/

struct editorConfig {
	struct termios orig_termios;
};

struct editorConfig E;


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
char editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}
	
	return c;
}


/*** output ***/

void editorDrawRows() {
	int y;
	for (y = 0; y < 24; y++) {
		write(STDOUT_FILENO, "~\r\n", 3);
	}
}


void editorRefreshScreen() {
	write(STDOUT_FILENO, "\x1b[2J", 4);		// the first byte  "\x1b" is the escape character (27 in decimal)
	write(STDOUT_FILENO, "\x1b[H", 3);										// other 3 bytes is "[2J"
							// escape sequence always starts with escape character (27) followed by [.
							// J command (Erase In Display) to clear screen. 2 says clear entire screen
							// H command positions cursor
	editorDrawRows();

	write(STDOUT_FILENO, "\x1b[H", 3);

}


/*** input ***/

// waits for a keypress and then handles it
void editorProcessKeypress() {
	char c = editorReadKey();

	switch (c) {
		case CTRL_KEY('q'):

			write(STDOUT_FILENO, "\x1b[2J", 4);
			write(STDOUT_FILENO, "\x1b[H", 3);		

			exit(0);
			break;
	}
}


/*** init ***/

int main() {

	enableRawMode();

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}
