#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include <libevdev/libevdev.h>
#include <libevdev/libevdev-uinput.h>
#include <xwiimote.h>

#include "config.h"

#define ABSMAX 98
#define SUPPORTED_IFACES (XWII_IFACE_CORE | XWII_IFACE_NUNCHUK | XWII_IFACE_CLASSIC_CONTROLLER)
#define DEFAULT_KEYMAP_PATH "default.cfg"

int max_retries = 3;

struct xwii_iface *iface;

struct map_data keymap_core[XWII_KEY_NUM],
	keymap_nunchuk[XWII_KEY_NUM],
	keymap_classic[XWII_KEY_NUM];
struct map_data *keymap;
struct controller_data controller_core,
	controller_nunchuk,
	controller_classic;
struct controller_data *controller_data;

struct libevdev_uinput *uinput_dev;

// Cleanup

static inline void cleanup_evdev() {
	if (uinput_dev) {
		libevdev_uinput_destroy(uinput_dev);
		uinput_dev = NULL;
	}
}
static inline void cleanup_wiimote() {
	if (iface) {
		// Close necessary interfaces
		xwii_iface_close(iface, xwii_iface_opened(iface));
		xwii_iface_unref(iface);
	}
}
static inline void cleanup() {
	cleanup_wiimote();
	cleanup_evdev();
}

// Error handling

static inline void w2g_error(int err, const char *msg) {
	if (err < 0) {
		errno = -err;
	} else {
		errno = err;
	}
	perror(msg);
	cleanup();
	exit(EXIT_FAILURE);
}
static void w2g_fail(const char *msg, ...) {
	va_list args;
	va_start(args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);
	cleanup();
	exit(EXIT_FAILURE);
}

static void sighandler(int signal) {
	if (signal == SIGINT) {
		cleanup();
	}
}

// Initialization


// From xwiishow.c
static char *get_dev(int num)
{
	struct xwii_monitor *mon;
	char *ent;
	int i = 0;

	mon = xwii_monitor_new(false, false);
	if (!mon)
		w2g_fail("Cannot create monitor\n");

	while ((ent = xwii_monitor_poll(mon))) {
		if (++i == num)
			break;
		free(ent);
	}

	xwii_monitor_unref(mon);

	if (!ent)
		w2g_fail("Cannot find device with number #%d\n", num);

	return ent;
}

static void init_keymap(const char *path) {
	ssize_t ret = read_config(path);
	if (ret) {
		if (ret < 0) {
			w2g_error(ret, "Error reading keymap");
		} else {
			w2g_fail("Error reading keymap on line %d\n", ret);
		}
	}
}

static void init_evdev() {
	struct libevdev *evdev;
	struct input_absinfo absinfo;
	int ret;
	int i;

	assert(NULL == uinput_dev);

	evdev = libevdev_new();
	// Axis parameters
	absinfo.value = 0;
	absinfo.minimum = -ABSMAX;
	absinfo.maximum = ABSMAX;
	absinfo.fuzz = 2;
	absinfo.flat = 4;
	absinfo.resolution = 1;
	// Set product id
	libevdev_set_name(evdev, controller_data->name);
	libevdev_set_id_vendor(evdev, controller_data->vendor);
	libevdev_set_id_product(evdev, controller_data->product);
	// Enable axis events
	libevdev_enable_event_type(evdev, EV_ABS);
	libevdev_enable_event_code(evdev, EV_ABS, ABS_X, &absinfo);
	libevdev_enable_event_code(evdev, EV_ABS, ABS_Y, &absinfo);
	// Enable key events
	libevdev_enable_event_type(evdev, EV_KEY);
	for (i = 0; i < XWII_KEY_NUM; ++i) {
		switch (keymap[i].intype) {
			case IN_TYPE_NONE:
				break;
			case IN_TYPE_KEY_OR_BTN:
				libevdev_enable_event_code(evdev, EV_KEY, keymap[i].input, NULL);
				break;
			case IN_TYPE_REL:
				libevdev_enable_event_code(evdev, EV_REL, keymap[i].input, NULL);
				break;
			case IN_TYPE_ABS:
				libevdev_enable_event_code(evdev, EV_ABS, keymap[i].input, &absinfo);
				break;
			default:
				w2g_fail("Unsupported intype %d\n", keymap[i].intype);
		}
	}

	ret = libevdev_uinput_create_from_device(evdev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput_dev); 
	if (ret) {
		libevdev_free(evdev);
		w2g_error(ret, "libevdev_uinput_create_from_device");
	}

	libevdev_free(evdev);
}

void load_keymap() {
	int available_ifaces = xwii_iface_available(iface) & SUPPORTED_IFACES;
	int opened_ifaces = xwii_iface_opened(iface);
	int tries = 0;
	int err;

	// Reopen devices
	// Have to repeatedly open interfaces because sometimes they aren't
	// immediately available
	while (err = xwii_iface_open(iface, available_ifaces)) {
		if (max_retries < ++tries) // True whenever the wiimote disconnects
			break;
		printf("Unable to open interfaces, retrying...\n");
		sleep(1);
	}

	opened_ifaces = xwii_iface_opened(iface);

	if ((opened_ifaces & available_ifaces) != available_ifaces) {
		printf("Unable to open some interfaces\n");
	}

	if (opened_ifaces & XWII_IFACE_CLASSIC_CONTROLLER) {
		keymap = keymap_classic;
		controller_data = &controller_classic;
	} else if (opened_ifaces & XWII_IFACE_NUNCHUK) {
		keymap = keymap_nunchuk;
		controller_data = &controller_nunchuk;
	} else {
		keymap = keymap_core;
		controller_data = &controller_core;
	}

	// Reload evdev device
	cleanup_evdev();
	init_evdev();

	if (opened_ifaces & XWII_IFACE_CLASSIC_CONTROLLER) {
		printf("Using Classic Controller\n");
	} else if (opened_ifaces & XWII_IFACE_NUNCHUK) {
		printf("Using Wiimote and Nunchuk\n");
	} else if (opened_ifaces & XWII_IFACE_CORE) {
		printf("Using Core Wiimote\n");
	}
}

static void init_wiimote(const char *devpath) {
	int ret;
	sigset_t blockset, oldset;

	// Disable SIGINT until finished initializing
	sigemptyset(&blockset);
	sigaddset(&blockset, SIGINT);
	sigprocmask(SIG_BLOCK, &blockset, &oldset);

	ret = xwii_iface_new(&iface, devpath);
	if (ret)
		w2g_error(ret, "Error initializing iface");

	// From xwiishow.c
	ret = xwii_iface_watch(iface, true);
	if (ret)
		w2g_error(ret, "Error: Cannot initialize hotplug watch descriptor");

	load_keymap();

	// Restore signal mask
	sigprocmask(SIG_SETMASK, &oldset, NULL);
}

// Event handlers

void handle_move(const struct xwii_event *ev) {
	const struct xwii_event_abs *absev = &ev->v.abs[0];
	libevdev_uinput_write_event(uinput_dev, EV_ABS, ABS_X, absev->x);
	libevdev_uinput_write_event(uinput_dev, EV_ABS, ABS_Y, -absev->y); // Inverted
	libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

void handle_key(const struct xwii_event *ev) {
	const struct xwii_event_key *keyev = &ev->v.key;
	
	struct map_data *mdata = keymap + keyev->code;
	
	int val;

	switch (mdata->intype) {
	case IN_TYPE_NONE:
		printf("Unmapped input\n");
		for (int i = 0; i < XWII_KEY_NUM; ++i) {
			printf("%d ", keymap[i].input);
		}
		printf("\n");
		break;
	case IN_TYPE_KEY_OR_BTN:
		if (keyev->state < 2) {
			// Treat as button
			val = mdata->reversed ? !keyev->state : keyev->state;
			libevdev_uinput_write_event(uinput_dev, EV_KEY, mdata->input, val);
		}
		break;
	case IN_TYPE_REL:
		w2g_fail("REL inputs are unsupported\n");
	case IN_TYPE_ABS:
		if (keyev->state) {
			val = ABSMAX;
			if (mdata->reversed)
				val = -val;
		} else {
			val = 0;
		}
		libevdev_uinput_write_event(uinput_dev, EV_ABS, mdata->input, val);
		break;
	default:
		w2g_fail("Unsupported input type %d\n", mdata->intype);
	}
	if (mdata->intype) {
		libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
	}
}


int main(int argc, const char *argv[]) {
	const char *devnum_str = NULL;
	int devnum;
	char *path;
	const char *keymap_path = NULL;
	const char *max_retries_str = NULL;
	int i;
	int ret;

	// Parse arguments
	for (i = 1; i < argc; ++i) {
		if (!strcmp("-m", argv[i])) {
			if (keymap_path)
				w2g_fail("Repeat option -m\n");
			keymap_path = argv[++i];
		} else if (!strcmp("-r", argv[i])) {
			if (max_retries_str)
				w2g_fail("Repeat option -r\n");
			max_retries_str = argv[++i];
		} else {
			if (devnum_str)
				w2g_fail("Too many wiimotes specified\n");
			devnum_str = argv[i];
		}
	}
	if (!devnum_str)
		w2g_fail("Usage: wii2gamepad [-m <keymap>] [-r <max retries>] <wiimote number>\n");

	if (!keymap_path) {
		keymap_path = DEFAULT_KEYMAP_PATH;
	}
	init_keymap(keymap_path);

	if (max_retries_str)
		max_retries = atoi(max_retries_str);
	devnum = atoi(devnum_str);
	path = get_dev(devnum);

	// Set up cleanup
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_mask = 0,
		.sa_flags = 0
	};
	sigaction(SIGINT, &sa, NULL);

	// Initializes the wiimote and the evdev object
	init_wiimote(path);

	struct pollfd pfd;
	pfd.fd = xwii_iface_get_fd(iface);
	pfd.events = POLL_IN;

	struct xwii_event ev;

	printf("Running (Press Ctrl-C to terminate)\n");
	
	while (1) {
		// Wait for an event
		ret = poll(&pfd, 1, -1);
		if (-1 == ret) {
			if (EINTR == errno) {
				cleanup();
				exit(EXIT_SUCCESS);
			}
			w2g_error(errno, "Unable to poll wiimote");
		}

		ret = xwii_iface_dispatch(iface, &ev, sizeof(ev));

		if (ret) {
			if (-EAGAIN == ret)
				continue;
			w2g_error(ret, "Unable to dispatch wiimote event");
		}

		switch (ev.type) {
		case XWII_EVENT_GONE:
			// Device is gone
			printf("Wiimote has disconnected\n");
			cleanup();
			exit(EXIT_SUCCESS);
		case XWII_EVENT_WATCH:
			load_keymap();
			break;
		case XWII_EVENT_NUNCHUK_MOVE:
			handle_move(&ev);
			break;
		case XWII_EVENT_KEY:
		case XWII_EVENT_NUNCHUK_KEY:
			handle_key(&ev);
			break;
		}
	}
}
