/* Set the LCD backlight brightness on the TrimUI Brick (tg5040) directly via
 * the display-engine ioctl, so the boot animation isn't dim before eros.elf
 * applies the configured brightness. Usage: setbright <raw 0-255> */
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define DISP_LCD_SET_BRIGHTNESS 0x102

int main(int argc, char *argv[])
{
	if (argc < 2) return 2;
	int val = atoi(argv[1]);
	if (val < 0) val = 0;
	if (val > 255) val = 255;
	int fd = open("/dev/disp", O_RDWR);
	if (fd < 0) return 1;
	unsigned long param[4] = { 0, (unsigned long)val, 0, 0 };
	ioctl(fd, DISP_LCD_SET_BRIGHTNESS, param);
	close(fd);
	return 0;
}
