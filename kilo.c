#include <termios.h>
#include <unistd.h>


void enableRawMode() {
	struct termios raw;

	tcgetattr(STDIN_FILENO, &raw); // read current attributes into a struct

	raw.c_lflag &= ~(ECHO);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	// TCSAFLUSH specifies when to apply the change

}  


int main() {

	enableRawMode();

	char c;
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');

	return 0;
}
