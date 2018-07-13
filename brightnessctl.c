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

static char *path = "/sys/class";
static char *classes[] = { "backlight", "leds", NULL };

static char *run_dir = "/tmp/brightnessctl";

struct value;
struct device;

static void fail(char *, ...);
static void usage(void);
#define cat_with(...) _cat_with(__VA_ARGS__, NULL)
static char *_cat_with(char, ...);
static char *dir_child(char *, char*);
static char *device_path(struct device *);
static char *class_path(char *);
static void apply_value(struct device *, struct value *);
static int apply_operation(struct device *, unsigned int, struct value *);
static bool parse_value(struct value *, char *);
static bool write_device(struct device *);
static bool read_device(struct device *, char *, char *);
static int read_class(struct device **, char *);
static int read_devices(struct device **);
static void print_device(struct device *);
static void list_devices(struct device **);
static struct device *find_device(struct device **, char *);
static bool save_device_data(struct device *);
static bool restore_device_data(struct device *);
static bool ensure_dir(char *);
#define ensure_run_dir() ensure_dir(run_dir)

struct device {
	char *class;
	char *id;
	unsigned int curr_brightness;
	unsigned int max_brightness;
};

enum value_type { ABSOLUTE, RELATIVE };
enum delta_type { DIRECT, DELTA };
enum sign { PLUS, MINUS };

struct value {
	unsigned long val;
	unsigned int v_type : 1;
	unsigned int d_type : 1;
	unsigned int sign : 1;
};

enum operation { INFO, GET, MAX, SET };

struct params {
	char *class;
	char *device;
	struct value val;
	long min;
	unsigned int quiet : 1;
	unsigned int list : 1;
	unsigned int pretend : 1;
	unsigned int mach : 1;
	unsigned int operation : 2;
	unsigned int save : 1;
	unsigned int restore : 1;
};

static struct params p;

static const struct option options[] = {
	{"class", required_argument, NULL, 'c'},
	{"device", required_argument, NULL, 'd'},
	{"help", no_argument, NULL, 'h'},
	{"list", no_argument, NULL, 'l'},
	{"machine-readable", no_argument, NULL, 'm'},
	{"min-value", optional_argument, NULL, 'n'},
	{"quiet", no_argument, NULL, 'q'},
	{"pretend", no_argument, NULL, 'p'},
	{"restore", no_argument, NULL, 'r'},
	{"save", no_argument, NULL, 's'},
	{"version", no_argument, NULL, 'V'},
	{NULL,}
};

int main(int argc, char **argv) {
	struct device *devs[255];
	struct device *dev;
	struct utsname name;
	char *dev_name, *file_path, *sys_run_dir;
	int n, c, phelp = 0;
	if (uname(&name))
		fail("Unable to determine current OS. Exiting!\n");
	if (strcmp(name.sysname, "Linux"))
		fail("This program only supports Linux.\n");
	while (1) {
		if ((c = getopt_long(argc, argv, "lqpmn::srhVc:d:", options, NULL)) < 0)
			break;
		switch (c) {
		case 'l':
			p.list = 1;
			break;
		case 'q':
			p.quiet = 1;
			break;
		case 'p':
			p.pretend = 1;
			break;
		case 's':
			p.save = 1;
			break;
		case 'r':
			p.restore = 1;
			break;
		case 'm':
			p.mach = 1;
			break;
		case 'n':
			if (optarg)
				p.min = atol(optarg);
			else
				p.min = 1;
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
	if (p.class) {
		if (!(n = read_class(devs, p.class)))
			fail("Failed to read any devices of class '%s'.\n", p.class);
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
		dev_name = devs[0]->id;
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
		fail("Invalid value given");
	if (!(dev = find_device(devs, dev_name)))
		fail("Device '%s' not found.\n", dev_name);
	if ((p.operation == SET || p.restore) && !p.pretend && geteuid()) {
		errno = 0;
		file_path = cat_with('/', path, dev->class, dev->id, "brightness");
		if (access(file_path, W_OK)) {
			perror("Can't modify brightness");
			fail("\nYou should run this program with root privileges.\n"
				"Alternatively, get write permissions for device files.\n");
		}
		free(file_path);
	}
	if ((sys_run_dir = getenv("XDG_RUNTIME_DIR")))
	    run_dir = dir_child(sys_run_dir, "brightnessctl");
	if (p.save)
		if (!save_device_data(dev))
			fprintf(stderr, "Could not save data for device '%s'.\n", dev->id);
	if (p.restore) {
		if (restore_device_data(dev))
			write_device(dev);
	}
	return apply_operation(dev, p.operation, &p.val);
}

int apply_operation(struct device *dev, unsigned int operation, struct value *val) {
	switch (operation) {
	case INFO:
		print_device(dev);
		return 0;
	case GET:
		fprintf(stdout, "%u\n", dev->curr_brightness);
		return 0;
	case MAX:
		fprintf(stdout, "%u\n", dev->max_brightness);
		return 0;
	case SET:
		apply_value(dev, val);
		if (!p.pretend)
			if (!write_device(dev))
				goto fail;
		if (!p.quiet) {
			if (!p.mach)
				fprintf(stdout, "Updated device '%s':\n", dev->id);
			print_device(dev);
			return 0;
		}
	/* FALLTHRU */
	fail:
	default:
		return 1;
	}
}

bool parse_value(struct value *val, char *str) {
	long n;
	char c;
	char *buf;
	errno = 0;
	val->v_type = ABSOLUTE;
	val->d_type = DIRECT;
	val->sign = PLUS;
	if (!str || !*str)
		return false;
	if (*str == '+' || *str == '-') {
		val->sign = *str == '+' ? PLUS : MINUS;
		val->d_type = DELTA;
		str++;
	}
	n = strtol(str, &buf, 10);
	if (errno || buf == str)
		return false;
	val->val = labs(n) % LONG_MAX;
	while ((c = *(buf++))) switch(c) {
	case '+':
		val->sign = PLUS;
		val->d_type = DELTA;
		break;
	case '-':
		val->sign = MINUS;
		val->d_type = DELTA;
		break;
	case '%':
		val->v_type = RELATIVE;
		break;
	}
	return true;
}

struct device *find_device(struct device **devs, char *name) {
	struct device *dev;
	while ((dev = *(devs++)))
		if (!fnmatch(name, dev->id, 0))
			return dev;
	return NULL;
}

void list_devices(struct device **devs) {
	struct device *dev;
	if (!p.mach)
		fprintf(stdout, "Available devices:\n");
	while ((dev = *(devs++)))
		print_device(dev);
}

void print_device(struct device *dev) {
	char *format = p.mach ? "%s,%s,%d,%d%%,%d\n":
		"Device '%s' of class '%s':\n\tCurrent brightness: %d (%d%%)\n\tMax brightness: %d\n\n";
	fprintf(stdout, format,
		dev->id, dev->class,
		dev->curr_brightness,
		(int) (100.0 * dev->curr_brightness / dev-> max_brightness),
		dev->max_brightness);
}

void apply_value(struct device *d, struct value *val) {
	long new, mod = val->v_type == ABSOLUTE ?
			val->val : ceil(val->val / 100.0 * d->max_brightness);
	if (val->d_type == DIRECT) {
		new = mod > d->max_brightness ? d->max_brightness : mod;
		goto apply;
	}
	mod *= val->sign == PLUS ? 1 : -1;
	new = d->curr_brightness + mod;
	if (new < p.min)
		new = p.min;
	if (new < 0)
		new = 0;
	if (new > d->max_brightness)
		new = d->max_brightness;
apply:
	d->curr_brightness = new;
}

bool write_device(struct device *d) {
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
	char *dev_path;
	int error = 0;
	struct dirent *ent;
	d->class = strdup(class);
	d->id = strdup(id);
	dev_path = device_path(d);
	if (!(dirp = opendir(dev_path)))
		goto dfail;
	while ((ent = readdir(dirp))) {
		if (!strcmp(ent->d_name, ".") && !strcmp(ent->d_name, ".."))
			continue;
		if (!strcmp(ent->d_name, "brightness")) {
			if ((f = fopen(dir_child(dev_path, ent->d_name), "r"))) {
				clearerr(f);
				if (fscanf(f, "%u", &d->curr_brightness) == EOF) {
					fprintf(stderr, "End-of-file reading brightness of device '%s'.", d->id);
					error++;
				} else if (ferror(f)) {
					fprintf(stderr, "Error reading brightness of device '%s': %s.", d->id, strerror(errno));
					error++;
				}
				fclose(f);
			} else
				goto fail;
		}
		if (!strcmp(ent->d_name, "max_brightness")) {
			if ((f = fopen(dir_child(dev_path, ent->d_name), "r"))) {
				clearerr(f);
				if (fscanf(f, "%u", &d->max_brightness) == EOF) {
					fprintf(stderr, "End-of-file reading max brightness of device '%s'.", d->id);
					error++;
				} else if (ferror(f)) {
					fprintf(stderr, "Error reading max brightness of device '%s': %s.", d->id, strerror(errno));
					error++;
				}
				fclose(f);
			} else
				goto fail;
		}
	}
	errno = 0;
fail:
	closedir(dirp);
dfail:
	if (errno) {
		perror("Error reading device");
		error++;
	}
	return error;
}

int read_class(struct device **devs, char *class) {
	DIR *dirp;
	struct dirent *ent;
	struct device *dev;
	int cnt = 0;
	dirp = opendir(class_path(class));
	if (!dirp)
		return 0;
	while ((ent = readdir(dirp))) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		dev = malloc(sizeof(struct device));
		if (!read_device(dev, class, ent->d_name)) {
			free(dev);
			continue;
		}
		devs[cnt++] = dev;
	}
	closedir(dirp);
	return cnt;
}

int read_devices(struct device **devs) {
	size_t n = 0;
	char *class;
	int cnt = 0;
	while ((class = classes[n++]))
		cnt += read_class(devs + cnt, class);
	return cnt;
}

bool save_device_data(struct device *dev) {
	char c[16];
	size_t s = sprintf(c, "%u", dev->curr_brightness);
	char *c_path = dir_child(run_dir, dev->class);
	char *d_path = dir_child(c_path, dev->id);
	FILE *fp;
	mode_t old = 0;
	int error = 0;
	errno = 0;
	if (s <= 0) {
		fprintf(stderr, "Error converting device data.");
		error++;
		goto fail;
	}
	if (!ensure_run_dir())
		goto fail;
	if (!ensure_dir(c_path))
		goto fail;
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
	free(c_path);
	free(d_path);
	if (errno) {
		perror("Error saving device data");
		error++;
	}
	return !error;
}

bool restore_device_data(struct device *dev) {
	char buf[16];
	char *filename = cat_with('/', run_dir, dev->class, dev->id);
	char *end;
	FILE *fp;
	memset(buf, 0, 16);
	errno = 0;
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
  -l, --list\t\t\tlist devices with available brightness controls.\n\
  -q, --quiet\t\t\tsuppress output.\n\
  -p, --pretend\t\t\tdo not perform write operations.\n\
  -m, --machine-readable\tproduce machine-readable output.\n\
  -n, --min-value\t\tset minimum brightness, defaults to 1.\n\
  -s, --save\t\t\tsave previous state in a temporary file.\n\
  -r, --restore\t\t\trestore previous saved state.\n\
  -h, --help\t\t\tprint this help.\n\
  -d, --device=DEVICE\t\tspecify device name (can be a wildcard).\n\
  -c, --class=CLASS\t\tspecify device class.\n\
  -V, --version\t\t\tprint version and exit.\n\
\n\
Operations:\n\
  i, info\t\t\tget device info.\n\
  g, get\t\t\tget current brightness of the device.\n\
  m, max\t\t\tget maximum brightness of the device.\n\
  s, set VALUE\t\t\tset brightness of the device.\n\
\n\
Valid values:\n\
  specific value\t\tExample: 500\n\
  percentage value\t\tExample: 50%%\n\
  specific delta\t\tExample: 50- or +10\n\
  percentage delta\t\tExample: 50%%- or +10%%\n\
\n");
}

