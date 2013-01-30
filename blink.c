/*
 * ThingM blink(1) command line utility.
 * Copyright 2013 Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef DEBUG
#define debug(msg, ...) fprintf(stderr, "DEBUG %s:%d: " msg "\n", \
		__func__, __LINE__, ##__VA_ARGS__)
#define error(msg, ...) fprintf(stderr, "ERROR %s:%d: " msg "\n", \
		__func__, __LINE__, ##__VA_ARGS__)
#else
#define debug(msg, ...) do { } while (0)
#define error(msg, ...) fprintf(stderr, msg "\n", ##__VA_ARGS__)
#endif /* DEBUG */

/* math utils */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define CLAMP(val, min, max) MAX((min), MIN((max), (val)))

/* color manipulation */
#define R(rgb) ((rgb & 0xFF0000) >> 16)
#define G(rgb) ((rgb & 0x00FF00) >> 8)
#define B(rgb) ((rgb & 0x0000FF) >> 0)

#define MSG_SIZE	9	/* blink(1) hidraw report size */
#define DURATION_MAX	0xffff	/* duration is stored on two 8-bit */

#define USAGE \
	"Usage: blink [OPTIONS] COMMAND [FIELD...]"

#define USAGE_OPTIONS \
	"Options:\n" \
	"\t-h\tprint this help message\n" \
	"\t-c\tlist defined colors"

#define USAGE_FADE \
	"Usage: blink c COLOR FADE\n" \
	"Example: blink c red 50"

#define USAGE_SET \
	"Usage: blink n COLOR\n" \
	"Example: blink n 454545"

#define USAGE_PLAY \
	"Usage: blink p 0|1 POSITION\n" \
	"Example: blink p 0 0 # Pause\n" \
	"         blink p 1 4 # Play from 5th position"

#define USAGE_PATT \
	"Usage: blink P COLOR FADE POSITION\n" \
	"Example: blink P green .5s 2 # 3rd pattern green with 500ms fade time"

#define USAGE_SDOWN \
	"Usage: blink D 0|1 DURATION\n" \
	"Example: blink D 0 0 # stop server tickle mode\n" \
	"         blink D 1 2000ms # start server tickle mode with 2s time"

#define COMMAND(c, n, u, d) { c, n, USAGE_##u, d }

const struct {
	const char cmd;
	const unsigned int argc;
	const char * const usage;
	const char * const desc;
} commands[] = {
	COMMAND('c', 2, FADE,	"Fade to RGB color"),
	COMMAND('D', 2, SDOWN,	"Serverdown tickle/off"),
	COMMAND('n', 1, SET,	"Set RGB color now"),
	COMMAND('p', 2, PLAY,	"Play/Pause"),
	COMMAND('P', 3, PATT,	"Set pattern entry"),
	{ /* sentinel */ }
};

const struct {
	const char * const name;
	const uint32_t value;
} colors[] = {
	{ "blue",	0x0000FF },
	{ "cyan",	0x00FFFF },
	{ "green",	0x00FF00 },
	{ "purple",	0xFF00FF },
	{ "red",	0xFF0000 },
	{ "white",	0xFFFFFF },
	{ "yellow",	0xFFFF00 },
	{ /* sentinel */ }
};

static int
parse_color(const char *color)
{
	unsigned long int value;
	char *end;
	int i;

	/* check if it is a defined color */
	for (i = 0; colors[i].name; ++i) {
		if (strcmp(colors[i].name, color) == 0) {
			debug("found defined color #%.6X (%s)",
					colors[i].value, colors[i].name);
			return colors[i].value;
		}
	}

	/* or check if it is a valid hexadecimal color value */
	value = strtoul(color, &end, 16);

	if (value > 0xFFFFFF)
		return -1;

	if (*color != '\0' && *end == '\0')
		return value;

	return -1;
}

/* Return a duration in hundredths of a second, as used by the blink(1) */
static int
parse_duration(const char *duration) {
	float value;
	char *end;

	value = strtof(duration, &end);

	/* if no unit, return the number as is */
	if (*duration != '\0' && *end == '\0')
		return CLAMP((int) value, -1, DURATION_MAX);

	/* 's' suffix means second */
	if (strcmp(end, "s") == 0)
		return CLAMP(value * 100, -1, DURATION_MAX);

	/* 'ms' suffix means millisecond */
	if (strcmp(end, "ms") == 0)
		return CLAMP(value / 10, -1, DURATION_MAX);

	return -1;
}

int
main(int argc, char *argv[]) {
	char buf[MSG_SIZE] = { 1, 0, 0, 0, 0, 0, 0, 0, 0 };
	unsigned int argr;
	int rgb, duration, play, position;
	char cmd;
	int i;
	int c;

	while ((c = getopt(argc, argv, "hc")) != -1) {
		switch (c) {
		case 'h':
			printf(USAGE "\n" USAGE_OPTIONS "\nCommands:\n");
			for (i = 0; commands[i].cmd; ++i)
				printf("\t%c\t%s\n", commands[i].cmd,
						commands[i].desc);
			return 0;
		case 'c':
			for (i = 0; colors[i].name; ++i)
				printf("%s\n", colors[i].name);
			return 0;
		default:
			error(USAGE_OPTIONS);
			return 1;
		}
	}

	if ((argr = argc - optind) == 0 || strlen(argv[optind]) != 1) {
		error("Put colors! Try 'blink -h' for more information.");
		return 1;
	}

	argr--;
	cmd = *argv[optind++];
	debug("command '%c'", cmd);
	buf[1] = cmd;

	/* quick check of commands syntax */
	for (i = 0; commands[i].cmd; ++i) {
		if (cmd == commands[i].cmd) {
			if (argr != commands[i].argc) {
				error("%s\n%s", commands[i].desc,
						commands[i].usage);
				return 1;
			}
		}
	}

	switch (cmd) {
	case 'P':
		/* set pattern entry */
		position = atoi(argv[optind + 2]);
		if (position < 0 || position > 11) {
			error("invalid position %d", position);
			return 1;
		}

		buf[7] = position;
		/* fall through... */
	case 'c':
		/* fade to RGB color */
		duration = parse_duration(argv[optind + 1]);
		if (duration < 0) {
			error("invalid duration");
			return 1;
		}

		buf[5] = duration >> 8;
		buf[6] = duration & 0xff;
		/* fall through... */
	case 'n':
		/* set RGB color now */
		rgb = parse_color(argv[optind]);

		buf[2] = R(rgb);
		buf[3] = G(rgb);
		buf[4] = B(rgb);
		break;
	case 'p':
		/* play/pause */
		play = atoi(argv[optind]);
		position = atoi(argv[optind + 1]);

		buf[2] = !!play;
		buf[3] = CLAMP(position, 0, 11);
		break;
	case 'D':
		/* serverdown mode */
		play = atoi(argv[optind]);
		duration = parse_duration(argv[optind + 1]);
		if (duration < 0) {
			error("invalid duration");
			return 1;
		}

		buf[2] = !!play;
		buf[3] = duration >> 8;
		buf[4] = duration & 0xff;
		break;
	default:
		error("unknown command '%c'. Try 'blink -h' for help.", cmd);
		return 1;
	}

	debug("message: %d %c %.2hhx %.2hhx %.2hhx %.2hhx %.2hhx %.2hhx %.2hhx",
			buf[0], buf[1], buf[2], buf[3], buf[4],
			buf[5], buf[6], buf[7], buf[8]);

	return write(STDOUT_FILENO, buf, MSG_SIZE) != MSG_SIZE ? 1 : 0;
}
