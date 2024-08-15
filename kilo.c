#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

// global variables
struct termios orig_termios;


// functions
void disableRawMode() {
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}


void enableRawMode() {

	tcgetattr(STDIN_FILENO, &orig_termios); // read current attributes into a struct
	atexit(disableRawMode); // atexit causes disableRawMode() to be called automatically when program exits.

	struct termios raw = orig_termios;
	raw.c_lflag &= ~(ECHO | ICANON); // turn off canonical mode (read input byte by bite, not line by line)

	raw.c_lflag &= ~(ECHO);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	// TCSAFLUSH specifies when to apply the change

}  


int main() {

	enableRawMode();

	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q') {
		if (iscntrl(c)) {	// prints non control characters
			printf("%d/n", c);
		}
		else {
			printf("%d ('%c')\n", c, c);
		}
	}

	return 0;
}
