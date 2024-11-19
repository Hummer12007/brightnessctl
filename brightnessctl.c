#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <getopt.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ENABLE_LOGIND
# if defined(HAVE_LIBSYSTEMD)
#  include <systemd/sd-bus.h>
# elif defined(HAVE_LIBELOGIND)
#  include <elogind/sd-bus.h>
# elif defined(HAVE_BASU)
#  include <basu/sd-bus.h>
# else
#  error "No dbus provider found"
# endif
#endif

static char *path = "/sys/class";
static char *classes[] = { "backlight", "leds", NULL };

static char *run_dir = "/tmp/brightnessctl";

struct value;
struct device;

enum operation;

static void fail(char *, ...);
static void usage(void);
static float parse_exponent(const char *);
#define cat_with(...) _cat_with(__VA_ARGS__, NULL)
static char *_cat_with(char, ...);
static char *dir_child(char *, char*);
static char *device_path(struct device *);
static char *class_path(char *);
static unsigned int calc_value(struct device *, struct value *);
static int process_device(struct device *);
static int apply_operation(struct device *, enum operation, struct value *);
static bool parse_value(struct value *, char *);
static bool do_write_device(struct device *);
static bool read_device(struct device *, char *, char *);
static int read_class(struct device **, char *, bool);
static int read_devices(struct device **);
static bool read_single_device(struct device **);
static void print_device(struct device *);
static void list_devices(struct device **);
static float val_to_percent(float, struct device *, bool);
static unsigned long percent_to_val(float, struct device *);
static bool find_devices(struct device **, char *);
static bool save_device_data(struct device *);
static bool restore_device_data(struct device *);
static bool ensure_dir(char *);
static bool ensure_dev_dir(struct device *);

#ifdef ENABLE_LOGIND
static bool logind_set_brightness(struct device *);
#endif

struct device {
	char *class;
	char *id;
	unsigned int curr_brightness;
	unsigned int max_brightness;
	bool matches;
};

enum value_type { ABSOLUTE, RELATIVE };
enum delta_type { DIRECT, PLUS, MINUS };

struct value {
	union {
		unsigned long val;
		float percentage;
	};
	enum value_type v_type;
	enum delta_type d_type;
};

enum operation { INFO, GET, MAX, SET, RESTORE };

struct params {
	char *class;
	char *device;
	struct value val;
	struct value min;
	enum operation operation;
	bool quiet;
	bool list;
	bool pretend;
	bool mach;
	bool percentage;
	bool save;
	bool restore;
	bool frac;
	float exponent;
};

static struct params p;

static const struct option options[] = {
	{"class", required_argument, NULL, 'c'},
	{"device", required_argument, NULL, 'd'},
	{"help", no_argument, NULL, 'h'},
	{"list", no_argument, NULL, 'l'},
	{"machine-readable", no_argument, NULL, 'm'},
	{"percentage", no_argument, NULL, 'P'},
	{"min-value", optional_argument, NULL, 'n'},
	{"exponent", optional_argument, NULL, 'e'},
	{"quiet", no_argument, NULL, 'q'},
	{"pretend", no_argument, NULL, 'p'},
	{"restore", no_argument, NULL, 'r'},
	{"save", no_argument, NULL, 's'},
	{"version", no_argument, NULL, 'V'},
	{"frac", no_argument, NULL, 0},
	{NULL,}
};

static bool (*write_device)(struct device *) = do_write_device;

int main(int argc, char **argv) {
	struct device *devs[255];
	struct device **devp = devs;
	struct device *dev;
	char *dev_name;
	int ret = 0;
	int n, c, phelp = 0;
	p.exponent = 1;
	p.min = (struct value){ .val = 0, .v_type = ABSOLUTE, .d_type = DIRECT };
	while ((c = getopt_long(argc, argv, "lqpmPn::e::srhVc:d:", options, NULL)) >= 0) {
		switch (c) {
		case 'l':
			p.list = true;
			break;
		case 'q':
			p.quiet = true;
			break;
		case 'p':
			p.pretend = true;
			break;
		case 's':
			p.save = true;
			break;
		case 'r':
			p.restore = true;
			break;
		case 'm':
			p.mach = true;
			break;
		case 'P':
			p.percentage = true;
			break;
		case 'n':
			if (optarg) {
				if (!parse_value(&p.min, optarg) || p.min.d_type == MINUS)
					fail("Invalid min-value given\n");
			} else if (NULL != argv[optind] && '-' != argv[optind][0]) {
				if (!parse_value(&p.min, argv[optind++]) || p.min.d_type == MINUS)
					fail("Invalid min-value given\n");
			} else {
				p.min.val = 1;
			}
			break;
		case 'e':
			if (optarg)
				p.exponent = parse_exponent(optarg);
			else if (NULL != argv[optind] && parse_exponent(argv[optind]) > 0.0)
				p.exponent = parse_exponent(argv[optind++]);
			else
				p.exponent = 4;
			break;
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case 'c':
			p.class = strdup(optarg);
			break;
		case 'd':
			p.device = strdup(optarg);
			break;
		case 'V':
			printf("%s\n", VERSION);
			exit(0);
			break;
		case 0:
			p.frac = true;
			break;
		default:
			phelp++;
		}
	}
	if (phelp) {
		usage();
		exit(EXIT_FAILURE);
	}
	argc -= optind;
	argv += optind;
	if (p.device && !strcmp(p.device, "*") && !p.class) {
		if (!p.quiet)
			fprintf(stderr, "Defaulting to the 'backlight' class, specify `-c '*'` to include leds.\n\n");
		p.class = strdup("backlight");
	}
	if (p.class && strcmp(p.class, "*")) {
		if (!(n = read_class(devs, p.class, true)))
			fail("Failed to read any devices of class '%s'.\n", p.class);
	} else if (!p.list && !p.class && !p.device) {
		if (!read_single_device(devs))
			fail("Failed to find a suitable device.\n");
		n = 1;
	} else {
		if (!(n = read_devices(devs)))
			fail("Failed to read any devices.\n");
	}
	devs[n] = NULL;
	if (p.list) {
		list_devices(devs);
		return 0;
	}
	dev_name = p.device;
	if (!dev_name)
		dev_name = p.class ? "*" : devs[0]->id;
	if (argc == 0)
		p.operation = INFO;
	else switch (argv[0][0]) {
	case 'm':
		p.operation = MAX; break;
	case 's':
		p.operation = SET; break;
	case 'g':
		p.operation = GET; break;
	default:
	case 'i':
		p.operation = INFO; break;
	}
	argc--;
	argv++;
	if (p.operation == SET && argc == 0)
		fail("You need to provide a value to set.\n");
	if (p.operation == SET && !parse_value(&p.val, argv[0]))
		fail("Invalid value given\n");
	if (!(find_devices(devs, dev_name)))
		fail("Device '%s' not found.\n", dev_name);
	while ((dev = *(devp++)))
		if (dev->matches)
			ret |= process_device(dev);
	return ret;
}

int process_device(struct device *dev) {
	char *file_path;
	if ((p.operation == SET || p.restore) && !p.pretend && geteuid()) {
		errno = 0;
		file_path = cat_with('/', path, dev->class, dev->id, "brightness");
		if (access(file_path, W_OK)) {
#ifdef ENABLE_LOGIND
			write_device = logind_set_brightness;
#else
			perror("Can't modify brightness");
			fail("\nYou should run this program with root privileges.\n"
				"Alternatively, get write permissions for device files.\n");
#endif
		}
		free(file_path);
	}
	if (p.save)
		if (!save_device_data(dev))
			fprintf(stderr, "Could not save data for device '%s'.\n", dev->id);
	if (p.restore) {
		if (restore_device_data(dev))
			p.operation = RESTORE;
	}
	return apply_operation(dev, p.operation, &p.val);
}

int apply_operation(struct device *dev, enum operation operation, struct value *val) {
	switch (operation) {
	case INFO:
		print_device(dev);
		return 0;
	case GET:
		if (p.percentage)
			fprintf(stdout, "%.4g\n", val_to_percent(dev->curr_brightness, dev, !p.frac));
		else
			fprintf(stdout, "%u\n", dev->curr_brightness);
		return 0;
	case MAX:
		fprintf(stdout, "%u\n", dev->max_brightness);
		return 0;
	case SET:
		dev->curr_brightness = calc_value(dev, val);
	/* FALLTHRU */
	case RESTORE:
		if (!p.pretend)
			if (!write_device(dev))
				goto fail;
		if (!p.quiet) {
			if (!p.mach)
				fprintf(stdout, "Updated device '%s':\n", dev->id);
			print_device(dev);
		}
		return 0;
	/* FALLTHRU */
	fail:
	default:
		return 1;
	}
}

bool parse_value(struct value *val, char *str) {
	double n;
	char c;
	char *buf;
	errno = 0;
	val->v_type = ABSOLUTE;
	val->d_type = DIRECT;
	if (!str || !*str)
		return false;
	if (*str == '+' || *str == '-') {
		val->d_type = *str == '+' ? PLUS : MINUS;
		str++;
	}
	n = strtod(str, &buf);
	if (errno || buf == str)
		return false;
	while ((c = *(buf++))) switch(c) {
	case '+':
		val->d_type = PLUS;
		break;
	case '-':
		val->d_type = MINUS;
		break;
	case '%':
		val->v_type = RELATIVE;
		break;
	}
	if (val->v_type == RELATIVE) {
		val->percentage = n;
	} else {
		val->val = labs((long) n) % LONG_MAX;
	}
	return true;
}

bool find_devices(struct device **devs, char *name) {
	struct device *dev;
	bool found = false;
	while ((dev = *(devs++)))
		if (!fnmatch(name, dev->id, 0))
			found = dev->matches = true;
	return found;
}

void list_devices(struct device **devs) {
	struct device *dev;
	if (!p.mach)
		fprintf(stdout, "Available devices:\n");
	while ((dev = *(devs++)))
		print_device(dev);
}

float val_to_percent(float val, struct device *d, bool rnd) {
	if (val < 0)
		return 0;
	float ret = powf(val / d->max_brightness, 1.0f / p.exponent) * 100;
	return rnd ? roundf(ret) : ret;
}

unsigned long percent_to_val(float percent, struct device *d) {
	return roundf(powf(percent / 100, p.exponent) * d->max_brightness);
}

void print_device(struct device *dev) {
	char *format = p.mach ? "%s,%s,%d,%.4g%%,%d\n" :
		"Device '%s' of class '%s':\n\tCurrent brightness: %d (%.4g%%)\n\tMax brightness: %d\n\n";
	fprintf(stdout, format,
		dev->id, dev->class,
		dev->curr_brightness,
		val_to_percent(dev->curr_brightness, dev, !p.frac),
		dev->max_brightness);
}

unsigned int calc_value(struct device *d, struct value *val) {
	long new = d->curr_brightness;
	if (val->d_type == DIRECT) {
		new = val->v_type == ABSOLUTE ? val->val : percent_to_val(val->percentage, d);
		goto apply;
	}
	long mod = val->val;
	if (val->d_type == MINUS)
		mod *= -1;
	if (val->v_type == RELATIVE) {
		mod = percent_to_val(val_to_percent(d->curr_brightness, d, false) + mod, d) - d->curr_brightness;
		if (val->val != 0 && mod == 0)
			mod = val->d_type == PLUS ? 1 : -1;
	}
	new += mod;
apply:
	if (p.min.v_type == RELATIVE) {
		p.min.val = percent_to_val(p.min.percentage, d);
		p.min.v_type = ABSOLUTE;
	}
	if (new < (long)p.min.val)
		new = p.min.val;
	if (new < 0)
		new = 0;
	if (new > d->max_brightness)
		new = d->max_brightness;
	return new;
}

#ifdef ENABLE_LOGIND

bool logind_set_brightness(struct device *d) {
	sd_bus *bus = NULL;
	int r = sd_bus_default_system(&bus);
	if (r < 0) {
		fprintf(stderr, "Can't connect to system bus: %s\n", strerror(-r));
		return false;
	}

	r = sd_bus_call_method(bus,
			       "org.freedesktop.login1",
			       "/org/freedesktop/login1/session/auto",
			       "org.freedesktop.login1.Session",
			       "SetBrightness",
			       NULL,
			       NULL,
			       "ssu",
			       d->class,
			       d->id,
			       d->curr_brightness);
	if (r < 0)
		fprintf(stderr, "Failed to set brightness: %s\n", strerror(-r));

	sd_bus_unref(bus);

	return r >= 0;
}

#endif

bool do_write_device(struct device *d) {
	FILE *f;
	char c[16];
	size_t s = sprintf(c, "%u", d->curr_brightness);
	errno = 0;
	if (s <= 0) {
		errno = EINVAL;
		goto fail;
	}
	if ((f = fopen(dir_child(device_path(d), "brightness"), "w"))) {
		if (fwrite(c, 1, s, f) < s)
			goto close;
	} else
		goto fail;
	errno = 0;
close:
	fclose(f);
fail:
	if (errno)
		perror("Error writing device");
	return !errno;
}

bool read_device(struct device *d, char *class, char *id) {
	DIR *dirp;
	FILE *f;
	char *dev_path = NULL;
	char *ent_path = NULL;
	int error = 0;
	struct dirent *ent;
	bool cur;
	d->class = strdup(class);
	d->id = strdup(id);
	dev_path = device_path(d);
	if (!(dirp = opendir(dev_path)))
		goto dfail;
	while ((ent = readdir(dirp))) {
		if ((cur = !strcmp(ent->d_name, "brightness")) ||
				!strcmp(ent->d_name, "max_brightness")) {
			if (!(f = fopen(ent_path = dir_child(dev_path, ent->d_name), "r")))
				goto fail;
			clearerr(f);
			if (fscanf(f, "%u", cur ? &d->curr_brightness : &d->max_brightness) == EOF) {
				fprintf(stderr, "End-of-file reading %s of device '%s'.",
						cur ? "brightness" : "max brightness", d->id);
				error++;
			} else if (ferror(f)) {
				fprintf(stderr, "Error reading %s of device '%s': %s.",
						cur ? "brightness" : "max brightness", d->id, strerror(errno));
				error++;
			}
			fclose(f);
			free(ent_path);
			ent_path = NULL;
		}
	}
	errno = 0;
fail:
	closedir(dirp);
dfail:
	free(dev_path);
	free(ent_path);
	if (errno) {
		perror("Error reading device");
		error++;
	}
	return !error;
}

int read_class(struct device **devs, char *class, bool read_all) {
	DIR *dirp;
	struct dirent *ent;
	struct device *dev;
	char *c_path;
	int cnt = 0;
	dirp = opendir(c_path = class_path(class));
	if (!dirp)
		return 0;
	while ((ent = readdir(dirp))) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		dev = calloc(1, sizeof(struct device));
		if (!read_device(dev, class, ent->d_name)) {
			free(dev);
			continue;
		}
		devs[cnt++] = dev;
		if (!read_all)
			break;
	}
	closedir(dirp);
	free(c_path);
	return cnt;
}

bool read_single_device(struct device **devs) {
	size_t n = 0;
	char *class;
	while ((class = classes[n++]))
		if (read_class(devs, class, false))
			return true;
	return false;
}

int read_devices(struct device **devs) {
	size_t n = 0;
	char *class;
	int cnt = 0;
	while ((class = classes[n++]))
		cnt += read_class(devs + cnt, class, true);
	return cnt;
}

bool save_device_data(struct device *dev) {
	char c[16];
	size_t s = sprintf(c, "%u", dev->curr_brightness);
	char *d_path = NULL;
	FILE *fp;
	mode_t old = 0;
	int error = 0;
	errno = 0;
	if (s <= 0) {
		fprintf(stderr, "Error converting device data.");
		error++;
		goto fail;
	}
	if (!ensure_dev_dir(dev))
		goto fail;
	d_path = cat_with('/', run_dir, dev->class, dev->id);
	old = umask(0);
	fp = fopen(d_path, "w");
	umask(old);
	if (!fp)
		goto fail;
	if (fwrite(c, 1, s, fp) < s) {
		fprintf(stderr, "Error writing to '%s'.\n", d_path);
		error++;
	}
	fclose(fp);
fail:
	free(d_path);
	if (errno) {
		perror("Error saving device data");
		error++;
	}
	return !error;
}

bool restore_device_data(struct device *dev) {
	char buf[16];
	char *filename = NULL;
	char *end;
	FILE *fp;
	memset(buf, 0, 16);
	errno = 0;
	if (!ensure_dev_dir(dev))
		goto fail;
	filename = cat_with('/', run_dir, dev->class, dev->id);
	if (!(fp = fopen(filename, "r")))
		goto fail;
	if (!fread(buf, 1, 15, fp))
		goto rfail;
	dev->curr_brightness = strtol(buf, &end, 10);
	if (end == buf)
		errno = EINVAL;
rfail:
	fclose(fp);
fail:
	free(filename);
	if (errno) {
		perror("Error restoring device data");
		return false;
	}
	return true;
}


bool ensure_dir(char *dir) {
	struct stat sb;
	if (stat(dir, &sb)) {
		if (errno != ENOENT)
			return false;
		errno = 0;
		if (mkdir(dir, 0777)) {
			return false;
		}
		if (stat(dir, &sb))
			return false;
	}
	if (!S_ISDIR(sb.st_mode)) {
		errno = ENOTDIR;
		return false;
	}
	return true;
}

static bool ensure_run_dir() {
	static bool set;
	if (!set) {
		char *sys_run_dir = getenv("XDG_RUNTIME_DIR");
		if (sys_run_dir)
			run_dir = dir_child(sys_run_dir, "brightnessctl");
		set = true;
	}
	return ensure_dir(run_dir);
}

bool ensure_dev_dir(struct device *dev) {
	char *cpath;
	bool ret;
	if (!ensure_run_dir())
		return false;
	cpath = dir_child(run_dir, dev->class);
	ret = ensure_dir(cpath);
	free(cpath);
	return ret;
}

char *_cat_with(char c, ...) {
	size_t size = 32;
	size_t length = 0;
	char *buf = calloc(1, size + 1);
	char *curr;
	char split[2] = {c, '\0'};
	va_list va;
	va_start(va, c);
	curr = va_arg(va, char *);
	while (curr) {
		length += strlen(curr);
		while (length + 2 > size)
			buf = realloc(buf, size *= 2);
		strcat(buf, curr);
		if ((curr = va_arg(va, char*))) {
			length++;
			strcat(buf, split);
		}
	}
	return buf;
}

char *dir_child(char *parent, char *child) {
	return cat_with('/', parent, child);
}

char *device_path(struct device *dev) {
	return cat_with('/', path, dev->class, dev->id);
}

char *class_path(char *class) {
	return dir_child(path, class);
}

static float parse_exponent(const char *str) {
	char *endptr = NULL;
	double ret;
	if ((ret = strtod(str, &endptr)) == 0)
		fail("Invalid exponent provided: %s\n", str);
	return ret;
}

void fail(char *err_msg, ...) {
	va_list va;
	va_start(va, err_msg);
	vfprintf(stderr, err_msg, va);
	va_end(va);
	exit(EXIT_FAILURE);
}

void usage() {
	fprintf(stderr, "brightnessctl %s - read and control device brightness.\n\n", VERSION);
	fprintf(stderr,
"Usage: brightnessctl [options] [operation] [value]\n\
\n\
Options:\n\
  -l, --list                 \tlist devices with available brightness controls.\n\
  -q, --quiet                \tsuppress output.\n\
  -p, --pretend              \tdo not perform write operations.\n\
  -m, --machine-readable     \tproduce machine-readable output.\n\
  -P, --percentage           \tdisplay value as a percentage in get.\n\
  -n, --min-value[=MIN-VALUE]\tset minimum brightness (to 1 if MIN-VALUE is omitted).\n\
  -e, --exponent[=K]         \tchanges percentage curve to exponential (to 4 if K is omitted).\n\
  -s, --save                 \tsave previous state in a temporary file.\n\
  -r, --restore              \trestore previous saved state.\n\
  -h, --help                 \tprint this help.\n\
  -d, --device=DEVICE        \tspecify device name (can be a wildcard).\n\
  -c, --class=CLASS          \tspecify device class.\n\
  -V, --version              \tprint version and exit.\n\
  --frac		     \tenable fractional percentage output.\n\
\n\
Operations:\n\
  i, info                    \tget device info.\n\
  g, get                     \tget current brightness of the device.\n\
  m, max                     \tget maximum brightness of the device.\n\
  s, set VALUE               \tset brightness of the device.\n\
\n\
Valid values:\n\
  specific value             \tExample: 500\n\
  percentage value           \tExample: 50%%\n\
  specific delta             \tExample: 50- or +10\n\
  percentage delta           \tExample: 50%%- or +10%%\n\
\n");
}
