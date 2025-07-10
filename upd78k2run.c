#include <stdlib.h>
#include "upd78k2.h"

int headless = 1;
void render_errorbox(char * title, char * buf)
{
}

void render_infobox(char * title, char * buf)
{
}

uint8_t pram[384];
uint8_t rom[0xFC80];

const memmap_chunk upd_map[] = {
	{ 0x0000, 0xFC80, 0xFFFFF, .flags = MMAP_READ, .buffer = rom},
	{ 0xFC80, 0xFD00, 0x7F, .flags = MMAP_READ | MMAP_WRITE | MMAP_CODE, .buffer = pram},
	{ 0xFD00, 0xFE00, 0xFF, .flags = MMAP_READ | MMAP_WRITE | MMAP_CODE, .buffer = pram + 128},
	{ 0xFF00, 0xFFFF, 0xFF, .read_8 = upd78237_sfr_read, .write_8 = upd78237_sfr_write}
};

int main(int argc, char **argv)
{
	long filesize;
	uint8_t *filebuf;
	upd78k2_options opts;
	upd78k2_context *upd;
	char *fname = NULL;
	uint8_t retranslate = 0;
	for (int i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-') {
			switch(argv[i][1])
			{
			case 'r':
				retranslate = 1;
				break;
			default:
				fprintf(stderr, "Unrecognized switch -%c\n", argv[i][1]);
				exit(1);
			}
		} else if (!fname) {
			fname = argv[i];
		}
	}
	if (!fname) {
		fputs("usage: ztestrun zrom [cartrom]\n", stderr);
		exit(1);
	}
	FILE * f = fopen(fname, "rb");
	if (!f) {
		fprintf(stderr, "unable to open file %s\n", fname);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	fseek(f, 0, SEEK_SET);
	filesize = filesize < sizeof(rom) ? filesize : sizeof(rom);
	if (fread(rom, 1, filesize, f) != filesize) {
		fprintf(stderr, "error reading %s\n",fname);
		exit(1);
	}
	fclose(f);
	init_upd78k2_opts(&opts, upd_map, 4);
	upd = init_upd78k2_context(&opts);
	upd->pc = rom[0] | rom[1] << 8;
	upd78k2_execute(upd, 10000000);
	return 0;
}