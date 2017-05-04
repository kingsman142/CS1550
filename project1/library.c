#include "iso_font.h" //Apple's supplied font

#include <unistd.h> //Contains STDIN and STDOUT constants
#include <fcntl.h>
#include <termios.h>

#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define MAKE_COLOR(r, g, b) ((color_t) (r << 11) | (g << 5) | (b))

typedef unsigned short color_t;

int framebuffer_desc; //The framebuffer device file descriptor
color_t* framebuffer; //The actual address of the framebuffer in memory
int size_of_display; //Size of the display's space when mapping the framebuffer to our address space
struct termios terminal_settings; //Settings of the terminal; mostly ICANON and ECHO will be used
struct fb_var_screeninfo screen_var_info;
struct fb_fix_screeninfo screen_fix_info;

void clear_screen();

void init_graphics(){
	framebuffer_desc = open("/dev/fb0", O_RDWR); //Open the framebuffer and get the file descriptor

	ioctl(framebuffer_desc, FBIOGET_VSCREENINFO, &screen_var_info);
	ioctl(framebuffer_desc, FBIOGET_FSCREENINFO, &screen_fix_info);

	ioctl(STDIN_FILENO, TCGETS, &terminal_settings); //Get the current terminal settings
	terminal_settings.c_lflag &= ~ICANON; //Switch ICANON off
	terminal_settings.c_lflag &= ~ECHO; //Switch ECHO off
	ioctl(STDIN_FILENO, TCSETS, &terminal_settings); //Set the new terminal settings

	size_of_display = screen_var_info.yres_virtual * screen_fix_info.line_length; //Set the size of the display

	framebuffer = (color_t*) mmap(NULL, size_of_display, PROT_WRITE, MAP_SHARED, framebuffer_desc, 0); //Maps the frame buffer in memory

	clear_screen(); //Make sure the screen is cleared so there's no text in the background
}

//Exit the graphics library and reset the settings of the frame to its default settings, including unmapping
void exit_graphics(){
	clear_screen(); //Clear the screen when we're done

	terminal_settings.c_lflag |= ICANON; //Switch ICANON on again
	terminal_settings.c_lflag |= ECHO; //Switch ECHO on again
	ioctl(STDIN_FILENO, TCSETS, &terminal_settings); //Set these new settings

	munmap(framebuffer, size_of_display); //Unmap the framebuffer from our address space
	close(framebuffer_desc); //Close the file descriptor of the framebuffer
}

//Clear the screen of all its current color
void clear_screen(){
	write(STDOUT_FILENO, "\033[2J", 4); //Tells the terminal to clear itself by printing out to the standard output; the first part of the string is an octal escape sequence
}

//Check to see if there is a character waiting in the buffer.
//If a character is there, return it to the caller.
char getkey(){
	fd_set rfds; //File descriptor set manages which file descriptors are being used; is passed into select()
	FD_ZERO(&rfds); //Clear the above set just to be safe
	FD_SET(0, &rfds); //Add file descriptor 0 (stdin) to the descriptor set

	struct timeval time_wait;
	time_wait.tv_sec = 5; //Wait 25seconds
	time_wait.tv_usec = 0; //Wait 0 microseconds
	int char_in_buffer = select(STDIN_FILENO+1, &rfds, NULL, NULL, &time_wait); //Check 1 device (file descriptor 0, a.k.a. stdin); first parameter must be highest file descriptor device + 1

	char buffered_char = '\0';

	if(char_in_buffer){ //There is a character in the stdin buffer
		read(0, &buffered_char, sizeof(buffered_char)); //Read 1 byte of data from file descriptor 0 (stdin) and store it in buffered_char
	}

	return buffered_char;
}

//Sleep for a specified number of milliseconds
void sleep_ms(long ms){
	//Cannot sleep for negative time
	if(ms < 0){
		return;
	}

	//Create the struct to hold the nanoseconds we're sleeping for
	struct timespec sleep_time_struct;
	sleep_time_struct.tv_sec = 0; //Seconds to sleep

	//999,999,999 ns (1 second) is the longest you can nanosleep() at a given time,
	//so if the input number is less than that, sleeping can be done in one call
	if(ms <= 999){
		sleep_time_struct.tv_nsec = ms*1000000;
		nanosleep(&sleep_time_struct, NULL);
	} else{ //else, it will require a loop that calls nanosleep() several times
		while(ms > 0){ //There is still time left to sleep
			ms -= 999;
			if(ms > 0){
				sleep_time_struct.tv_nsec = 999000000; //There is some time left over after subtracting 999,000,000 ns, so we can sleep for that time
			} else{
				sleep_time_struct.tv_nsec = (ms + 999)*1000000; //There is less than 999,000,000 ns left, so add it back on for now and sleep for that specific amount of nanoseconds
			}
			nanosleep(&sleep_time_struct, NULL);
		}
	}
}

//Draw 1 pixel located at the coordinate (x, y) with the a specified color
void draw_pixel(int x, int y, color_t color){
	//Do not try drawing a pixel off screen/out of bounds; it's unncessary
	if(x < 0 || y < 0 || x > screen_var_info.xres_virtual || y > screen_var_info.yres_virtual){
		return;
	}

	int offset = (y * screen_var_info.xres_virtual) + x; //Set the offset in order to later modify that address using pointer arithmetic
	color_t* pixel = framebuffer + offset; //Navigate to necessary address
	*pixel = color; //Set the color of the necessary pixel
}

//Draw a rectangle located at (x, y) with specified height, width, and unsigned 16-bit color
void draw_rect(int x1, int y1, int width, int height, color_t c){
	//Return if the rectangle will be completely off screen
	if(((x1+width) < 0 && (y1+height) < 0) || (x1 > screen_var_info.xres_virtual && y1 > screen_var_info.yres_virtual)){
		return;
	}

	int i = 0;

	for(i = 0; i < width; i++){ //Draw top side of rectangle
		draw_pixel(x1++, y1, c);
	}

	for(i = 0; i < height; i++){ //Draw right side of rectangle
		draw_pixel(x1, y1++, c);
	}

	for(i = 0; i < width; i++){ //Draw bottom side of rectangle
		draw_pixel(x1--, y1, c);
	}

	for(i = 0; i < height; i++){ //Draw left side of rectangle
		draw_pixel(x1, y1--, c);
	}
}

//Draw a given character found in the iso_font array with given color c at location (x, y)
void draw_char(int x, int y, const char character, color_t c){
	int i = 0;
	int j = 0;

	for(i = 0; i < 16; i++){ //Iterate through the rows
		int curr_row = iso_font[character*16 + i]; //Get the pixel data for row i		

		for(j = 0; j < 8; j++){ //Iterate left through right in this specific row
			int draw_pixel_boolean = curr_row & 0x01; //AND the current row with 0x80 (1000 0000) to get the most significant bit
			curr_row >>= 1; //Shift the current row value right by 1 so we can continue to grab the least significant bit
			if(draw_pixel_boolean){ //If our AND'd bit is 1, draw the pixel there
				draw_pixel(x + j, y + i, c);
			}
		}
	}
}

//Draw a given piece of text onto the display at location (x, y) and color c
void draw_text(int x, int y, const char* text, color_t c){
	int i = 0;

	for(i = 0; text[i] != '\0'; i++){ //Iterate through the charactesr in the text
		draw_char(x + i*8, y, text[i], c); //Draw each character, moving 16 pixels to the right each time
	}
}
