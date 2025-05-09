#include <stdbool.h>
#include <stdio.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <xwiimote.h>

#include "config.h"
#include "keymap.h"
#include "util.h"

extern struct map_data keymap_core[XWII_KEY_NUM],
	keymap_nunchuk[XWII_KEY_NUM],
	keymap_classic[XWII_KEY_NUM];
struct map_data keymap_all[XWII_KEY_NUM];
extern struct controller_data controller_core,
	controller_nunchuk,
	controller_classic;
struct controller_data controller_all;

static int ext;

static inline int is_whitespace(char c) {
	return ' ' == c || '\t' == c;
}
static inline int is_alphanum(char c) {
	return ('A' <= c && 'Z' >= c) || ('0' <= c && '9' >= c) || '_' == c;
}

enum read_state {
	RS_INIT,
	RS_NORMAL,
	RS_COMMENT,
};

static void fnputs(FILE *stream, const char *s, size_t len) {
	for (int i = 0; i < len; ++i)
		fputc(s[i], stream);
}


static int interpret_label(const char *label, size_t label_len) {
	if (strmatch("None", label, label_len)) {
		ext = XWII_IFACE_CORE;
	} else if (strmatch("Nunchuk", label, label_len)) {
		ext = XWII_IFACE_NUNCHUK;
	} else if (strmatch("Classic Controller", label, label_len)) {
		ext = XWII_IFACE_CLASSIC_CONTROLLER;
	} else if (strmatch("All", label, label_len)) {
		ext = -1;
	} else {
		fprintf(stderr, "Extension ");
		fnputs(stderr, label, label_len);
		fprintf(stderr, " not recognized\n");
		return -1;
	}
	return 0;
}

static int read_controller_info(const char *left_token, size_t left_token_len, const char *right_token, size_t right_token_len) {
	struct controller_data *cdata;

	if (XWII_IFACE_CORE == ext) {
		cdata = &controller_core;
	} else if (XWII_IFACE_NUNCHUK == ext) {
		cdata = &controller_nunchuk;
	} else if (XWII_IFACE_CLASSIC_CONTROLLER == ext) {
		cdata = &controller_classic;
	} else if (-1 == ext) {
		cdata = &controller_all;
	} else {
		fprintf(stderr, "Internal error, ext not recognized\n");
		return -1;
	}
	
	if (strmatch("Name", left_token, left_token_len)) {
		if (cdata->name) {
			fprintf(stderr, "Name already specified\n");
			return -1;
		}
		// Copy name to cdata
		cdata->name = malloc(right_token_len + 1); // Not bothering to free()
		memcpy(cdata->name, right_token, right_token_len);
		cdata->name[right_token_len] = '\0';
	} else if (strmatch("Vendor", left_token, left_token_len)) {
		if (cdata->vendor) {
			fprintf(stderr, "Vendor already specified\n");
			return -1;
		}
		cdata->vendor = atoi(right_token);
	} else if (strmatch("Product", left_token, left_token_len)) {
		if (cdata->product) {
			fprintf(stderr, "Product ID already specified\n");
			return -1;
		}
		cdata->product = atoi(right_token);
	} else {
		return 1;
	}
	return 0;
}


/*
 * Convert the given Wiimote button into a keycode
 */
enum xwii_event_keys get_wii_key(const char *c, size_t len) {
	int i;
	for (i = 0; i < XWII_KEY_NUM; ++i) {
		if (strmatch(wii_key_map[i].key, c, len)) {
			return wii_key_map[i].value;
		}
	}
	return -1;
}

/*
 * Identify whether the token corresponds to a key/button, relative axis, or
 * absolute axis. Save this data to out.
 */
static int get_map_key(const char *c, size_t len, struct map_data *out) {
	int i;
	for (i = 0; map_key_map[i].key; ++i) {
		if (strmatch(map_key_map[i].key, c, len)) {
			out->intype = IN_TYPE_KEY_OR_BTN;
			out->input = map_key_map[i].value;
			return 0;
		}
	}
	for (i = 0; map_rel_map[i].key; ++i) {
		if (strmatch(map_rel_map[i].key, c, len)) {
			out->intype = IN_TYPE_REL;
			out->input = map_rel_map[i].value;
			return 0;
		}
	}
	for (i = 0; map_abs_map[i].key; ++i) {
		if (strmatch(map_abs_map[i].key, c, len)) {
			out->intype = IN_TYPE_ABS;
			out->input = map_abs_map[i].value;
			return 0;
		}
	}
	fprintf(stderr, "Key ");
	fnputs(stderr, c, len);
	fprintf(stderr, " not understood\n", c);
	return -1;
}

static int store_key(enum xwii_iface_type ext, enum xwii_event_keys wii_key, struct map_data *mdata) {
	struct map_data *map;
	if (XWII_IFACE_CORE == ext) {
		map = keymap_core;
	} else if (XWII_IFACE_NUNCHUK == ext) {
		map = keymap_nunchuk;
	} else if (XWII_IFACE_CLASSIC_CONTROLLER == ext) {
		map = keymap_classic;
	} else if (-1 == ext) {
		map = keymap_all;
	} else {
		fprintf(stderr, "Internal error, ext not recognized\n");
		return -1;
	}

	// Check for previous entry
	if (map[(long) wii_key].intype) {
		fprintf(stderr, "Duplicate entry\n");
		return -1;
	}

	memcpy(map + wii_key, mdata, sizeof(struct map_data));
	return 0;
}

static int read_mapped_key(const char *left_token, size_t left_token_len,
		const char *right_token, size_t right_token_len) {
	int wii_key;
	struct map_data mdata;
	int err;

	wii_key = get_wii_key(left_token, left_token_len);
	if (wii_key == -1) {
		return wii_key;
	}

	// Read - sign -- reverse input
	if ('-' == right_token[0]) {
		mdata.reversed = true;
		++right_token;
		--right_token_len;
	} else {
		mdata.reversed = false;
	}
	if (err = get_map_key(right_token, right_token_len, &mdata)) {
		return err;
	}
	// Store key
	if (err = store_key(ext, wii_key, &mdata)) {
		return err;
	}
	return 0;
}

static int interpret_line(const char *left_token, size_t left_token_len,
		const char *right_token, size_t right_token_len) {
	int err;
	if (!(err = read_controller_info(left_token, left_token_len, right_token, right_token_len))) {
		return 0;
	}
	if (1 == err && 0 == read_mapped_key(left_token, left_token_len, right_token, right_token_len)) {
		return 0;
	}
	return -1;	
}

/*
 * Returns 0 on success or a positive line number on error.
 */
static ssize_t parse_config(char *file, size_t filelen) {
	const char *c = file, *c2;
	size_t line = 1;
	const char *left_token, *right_token, *label;
	size_t left_token_len, right_token_len, label_len;
	enum read_state state = RS_INIT;
	const char *equals, *line_end;
	int err;

	
	while (c < file + filelen) {
		// Skip whitespace
		while (c < file + filelen && (is_whitespace(*c) || '\n' == *c)) {
			if ('\n' == *c)
				++line;
			++c;
		}
		if (c == file + filelen)
			return 0;

		if (*c == ';') {
		 state = RS_COMMENT;
		}

		switch (state) {
		case RS_INIT:
			// Read a section label
			if ('[' != *c)
				return line;

			state = RS_NORMAL;
			// No break
		case RS_NORMAL:
			// Read label
			if ('[' == *c) {
				++c;
				label = c;
				while (']' != *c) {
					if (*c == '\n' || ';' == *c) {
						fprintf(stderr, "] expected\n");
						return line;
					}
					++c;
				}
				assert (']' == *c);
				label_len = c - label;

				if (interpret_label(label, label_len)) {
					return line;
				}
				++c;
			} else {
				// Read regular line
				left_token = c;

				// Find equals sign
				while ('=' != *c) {
					if ('\n' == *c) {
						fprintf(stderr, "= expected\n");
						return line;
					}
					++c;
				}
				equals = c;

				// Find the end of the string
				for  (c = equals - 1; is_whitespace(*c); --c);
				++c;
				left_token_len = c - left_token;

				// Skip whitespace
				for (c = equals + 1; is_whitespace(*c); ++c) {
					if ('\n' == *c || ';' == *c) {
						fprintf(stderr, "Right token expected\n");
						return line;
					}
				}
				
				// Find next token
				right_token = c;
				// Find the end of the line
				while ('\n' != *c && ';' != *c) {
					++c;
				}
				line_end = c;
				// Denote the end of the string
				--c;
				while (is_whitespace(*c)) {
					--c;
				}
				++c;
				right_token_len = c - right_token;
				// Skip to the end of the line
				c = line_end;
				
				// Interpret and store the key
				if (interpret_line(left_token, left_token_len, right_token,
						right_token_len)) {
					printf("Failed to interpret line\n");
					return line;
				}
			}

			break;

		case RS_COMMENT:
			assert(*c == ';');
			while ('\n' != *c)
				++c;
			if (ext) {
				state = RS_NORMAL;
			} else {
				state = RS_INIT;
			}
			break;
		} // switch

		// No other tokens allowed
		if ('\0' != *c && '\n' != *c && ';' != *c) {
			fprintf(stderr, "Extra token\n");
			return line;
		}
	} // while



	return 0;
}

static void replace_if_zero(void *dest, void *src, size_t size) {
	size_t i;
	for (i = 0; i < size; ++i) {
		if (((char *) dest)[i]) {
			return;
		}
	}
	memcpy(dest, src, size);
}

static int set_defaults() {
	int i;
	for (i = 0; i < XWII_KEY_NUM; ++i) {
		if (keymap_all[i].intype) {
			replace_if_zero(keymap_core + i, keymap_all + i, sizeof(struct map_data));
			replace_if_zero(keymap_nunchuk + i, keymap_all + i, sizeof(struct map_data));
			replace_if_zero(keymap_classic + i, keymap_all + i, sizeof(struct map_data));
		}
	}
	if (controller_all.name) {
		replace_if_zero(&controller_core.name, &controller_all.name, sizeof(controller_all.name));
		replace_if_zero(&controller_nunchuk.name, &controller_all.name, sizeof(controller_all.name));
		replace_if_zero(&controller_classic.name, &controller_all.name, sizeof(controller_all.name));
	} 
	if (controller_all.vendor) {
		replace_if_zero(&controller_core.vendor, &controller_all.vendor, sizeof(controller_all.vendor));
		replace_if_zero(&controller_nunchuk.vendor, &controller_all.vendor, sizeof(controller_all.vendor));
		replace_if_zero(&controller_classic.vendor, &controller_all.vendor, sizeof(controller_all.vendor));
	} 
	if (controller_all.product) {
		replace_if_zero(&controller_core.product, &controller_all.product, sizeof(controller_all.product));
		replace_if_zero(&controller_nunchuk.product, &controller_all.product, sizeof(controller_all.product));
		replace_if_zero(&controller_classic.product, &controller_all.product, sizeof(controller_all.product));
	}
}

ssize_t read_config(const char *path) {
	int fd = open(path, O_RDONLY);
	if (-1 == fd)
		return errno;
	

	struct stat statbuf;
	fstat(fd, &statbuf);
	size_t filelen = statbuf.st_size;
	
	// Map entire file (dangerous?)
	char * const file = mmap(NULL, filelen, PROT_READ, MAP_PRIVATE, fd, 0);

	ssize_t ret = parse_config(file, filelen);

	if (-1 == munmap(file, filelen))
		return -errno;
	if (-1 == close(fd))
		return -errno;

	set_defaults();

	return ret;
}
