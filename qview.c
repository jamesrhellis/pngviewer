/*******************************************************************************
* Copyright 2017 James RH Ellis
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal 
* in the Software without restriction, including without limitation the rights 
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
* copies of the Software, and to permit persons to whom the Software is 
* furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in all 
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
* SOFTWARE.
*******************************************************************************/
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <png.h>
#include <stdint.h>

typedef struct {
	uint8_t r,g,b,a;
} colour;

typedef struct {
	uint32_t w, h;
	colour *pixels;
} canvas;

typedef struct {
	uint32_t size;
	colour *items;
} pallet;

typedef struct {
	uint32_t x, y;
	uint32_t w, h;
} window;

int init_canvas(canvas *c, char *file) {
	FILE *f = fopen(file, "rb");
	if (!f) {
		return 0;
	}

	unsigned char head[8];
	if (fread(head, 1, 8, f) < 8 || png_sig_cmp(head, 0, 8)) {
		fclose(f);
		return 0;
	}

	png_struct *png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fclose(f);
		return 0;
	}
	png_info *info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, NULL, NULL);
		fclose(f);
		return 0;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_read_struct(&png, &info, NULL);
		fclose(f);
		return 0;
	}

	png_init_io(png, f);
	png_set_sig_bytes(png, 8);
	
	png_read_info(png, info);
	
	uint32_t w, h;
	int bit_depth, colour_type;
	png_get_IHDR(png, info, &w, &h, &bit_depth, &colour_type, NULL, NULL, NULL);

	//png_get_PLTE
	//png_get_tRNS
	
	if (bit_depth > 8) {
		png_set_scale_16(png);
	}

	switch (colour_type) {
		case PNG_COLOR_TYPE_PALETTE:
			png_set_expand(png);
			if (!png_get_valid(png, info, PNG_INFO_tRNS)) {
				png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
			} // tRNS is made alpha by png_set_expand
			break;
		case PNG_COLOR_TYPE_GRAY:
			// No need to expand as grey to rgb does this
			png_set_gray_to_rgb(png);
			if (!png_get_valid(png, info, PNG_INFO_tRNS)) {
				png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
			} else {
				png_set_tRNS_to_alpha(png);
			}
			break;
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			png_set_gray_to_rgb(png);
			break;
		case PNG_COLOR_TYPE_RGB:
			if (!png_get_valid(png, info, PNG_INFO_tRNS)) {
				png_set_add_alpha(png, 255, PNG_FILLER_AFTER);
			} else {
				png_set_tRNS_to_alpha(png);
			}
			break;
		case PNG_COLOR_TYPE_RGBA:
			break;
		default:
			png_destroy_read_struct(&png, &info, NULL);
			fclose(f);
			return 0;
	}

	// Interlaced images are probably rare - but incase
	png_set_interlace_handling(png);

	png_read_update_info(png, info);
	png_get_IHDR(png, info, &w, &h, &bit_depth, &colour_type, NULL, NULL, NULL);

	if (PNG_COLOR_TYPE_RGBA != colour_type
			|| 8 != bit_depth) {
		fprintf(stderr, "Failed to correctly change image format!");
	}

	uint8_t *pixels = malloc(png_get_rowbytes(png, info) * h);

	if (setjmp(png_jmpbuf(png))) {
		free(pixels);
		png_destroy_read_struct(&png, &info, NULL);
		fclose(f);
		return 0;
	}

	uint8_t *row = pixels;
	for (int i = 0;i < h;++i) {
		png_read_row(png, row, NULL);
		row += png_get_rowbytes(png, info);
	}
	
	*c = (canvas) {w, h, (colour *)pixels};

	png_read_end(png, NULL);
	png_destroy_read_struct(&png, &info, NULL);
	fclose(f);

	return 1;
}

int save_canvas(canvas *c, char *file) {
	FILE *f = fopen(file, "wb");
	if (!f) {
		return 0;
	}

	png_struct *png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fclose(f);
		return 0;
	}
	png_info *info = png_create_info_struct(png);
	if (!info) {
		png_destroy_write_struct(&png, NULL);
		fclose(f);
		return 0;
	}

	if (setjmp(png_jmpbuf(png))) {
		png_destroy_write_struct(&png, &info);
		fclose(f);
		return 0;
	}

	png_init_io(png, f);
	
	png_set_IHDR(png, info, c->w, c->h, 8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE
			, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_write_info(png, info);

	uint8_t *row = (uint8_t *)c->pixels;
	for (int i = 0;i < c->h;++i) {
		png_write_row(png, row);
		row += png_get_rowbytes(png, info);
	}
	
	png_write_end(png, NULL);
	png_destroy_write_struct(&png, &info);
	fclose(f);

	return 1;
}

static inline void set_colours(colour fore, colour bak) {
	printf("\033[38;2;%d;%d;%d;48;2;%d;%d;%dm", fore.r, fore.g, fore.b
			, bak.r, bak.g, bak.b);
}

static inline void print_pair(colour top, colour bot) {
	set_colours(bot, top);
	printf("▄");
}

static inline colour get_alpha_colour(uint32_t x, uint32_t y, colour col) {
	colour blend = {0xDD, 0xDD, 0xDD};
	if (((x & 1) + y) & 1) {
		blend = (colour) {0xAA, 0xAA, 0xAA};
	}

	col.r = (((uint32_t)col.r * col.a) + ((uint32_t)blend.r * (255 - col.a))) / 255;
	col.g = (((uint32_t)col.g * col.a) + ((uint32_t)blend.g * (255 - col.a))) / 255;
	col.b = (((uint32_t)col.b * col.a) + ((uint32_t)blend.b * (255 - col.a))) / 255;

	return col;
}

static inline void print_alpha_pair(uint32_t x, uint32_t yt, colour top, colour bot) {
	print_pair(get_alpha_colour(x, yt, top), get_alpha_colour(x, yt+1, bot));
}

static inline void reset_colour() {
	printf("\033[m");
}

static inline void move_to(uint32_t x, uint32_t y) {
	printf("\033[%u;%uf", x, y);
}

#define MIN(X, Y) ((X > Y)?Y:X)

static inline void print_canvas_window(canvas c, window win) {
	uint32_t w = MIN(win.w, c.w - win.x);
	uint32_t h = MIN(win.h, c.h - win.y - 1);
	for (int y = win.y;y < win.y + h;y += 2) {
		for (int x = win.x;x < win.x + w;++x) {
			print_alpha_pair(x, y, c.pixels[(y*c.w)+x], c.pixels[((y+1)*c.w)+x]);
		}
		printf("\033[%uD", w);
		printf("\033[B");
	}
	reset_colour();
}

int main(int argn, char **args) {
	if (argn < 2) {
		fprintf(stderr, "No file name given!\n");
		return 0;
	}

	if (!isatty(STDIN_FILENO)) {
		fprintf(stderr, "Not a terminal!\n");
		return 0;
	}

	struct termios old, new;
	tcgetattr(STDIN_FILENO, &old);
	tcgetattr(STDIN_FILENO, &new);

	new.c_lflag &= ~(ICANON|ECHO);
	new.c_cc[VMIN] = 1;
	new.c_cc[VTIME] = 0;

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &new);

	for (int i = 1;i < argn;++i) {
		char *file = args[i];

		canvas c;
		if (!init_canvas(&c, file)) {
			fprintf(stderr, "Unnable to load or create file!\n");
			return 0;
		}

		window win = {0};
		char ch;
		while (1) {
			struct winsize s;
			ioctl(STDOUT_FILENO, TIOCGWINSZ, &s);
			if (win.w != s.ws_col || win.h != s.ws_row << 1) {
				win.w = s.ws_col;
				win.h = s.ws_row << 1;

				move_to(0, 0);
				print_canvas_window(c, win);
				reset_colour();
			}

			read(STDIN_FILENO, &ch, 1);
			if (ch == '\004' || ch == 'q')
				break;
			else switch (ch) {
			case 's':
				win.y -= 1;
				break;
			case 'S':
				win.y -= 10;
				break;
			case 'w':
				win.y += 1;
				break;
			case 'W':
				win.y += 10;
				break;
			case 'a':
				win.x -= 1;
				break;
			case 'A':
				win.x -= 10;
				break;
			case 'd':
				win.x += 1;
				break;
			case 'D':
				win.x += 10;
				break;
			default:
				break;
			}
			win.x %= c.w;
			win.y %= c.h;

			move_to(0, 0);
			print_canvas_window(c, win);
			reset_colour();

			fflush(stdout);
		}
		/*
		if (!save_canvas(&c, "test.png")) {
			fprintf(stderr, "Unnable to save or create file!\n");
			return 0;
		}
		*/
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &old);
}
