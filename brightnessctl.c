#ifdef linux
#define _GNU_SOURCE
#endif
#include <sys/types.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#define str(x) #x

static char *path = "/sys/class";
static char *classes[] = { "backlight", "leds", NULL };
static char *default_device = "acpi_video0";

struct value;
struct device;

void fail(int, char *, ...);
void usage(void);
char *cat_with(char, ...);
char *dir_child(char *, char*);
char *device_path(struct device *);
char *class_path(char *);
void apply_value(struct device *, struct value *);
int apply_operation(struct device *, unsigned int, struct value *);
int parse_value(struct value *, char *);
int write_device(struct device *);
int read_device(struct device *, char *, char *);
int read_class(struct device **, char *);
int read_devices(struct device **);
int print_device(struct device *);
int list_devices(struct device **);
struct device *find_device(struct device **, char *);

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

enum operation { GET, MAX, SET };

struct params {
	char *class;
	char *device;
	struct value val;
	unsigned int quiet : 1;
	unsigned int list : 1;
	unsigned int pretend : 1;
	unsigned int mach : 1;
	unsigned int operation : 2;
};

static struct params p;

static const struct option options[] = {
	{"list", no_argument, NULL, 'l'},
	{"quiet", no_argument, NULL, 'q'},
	{"pretend", no_argument, NULL, 'p'},
	{"machine-readable", no_argument, NULL, 'm'},
	{"help", no_argument, NULL, 'h'},
	{"class", required_argument, NULL, 'c'},
	{"device", required_argument, NULL, 'd'},
};

int main(int argc, char **argv) {
	struct device *devs[255];
	struct device *dev;
	struct utsname name;
	char *dev_name;
	int n, c, phelp = 0;
	if (uname(&name))
		fail(-1, "Unable to determine current OS. Exiting!\n");
	if (strcmp(name.sysname, "Linux"))
		fail(-1, "This program only supports Linux.\n");
	while (1) {
		if ((c = getopt_long(argc, argv, "lqpmhc:d:", options, NULL)) < 0)
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
		case 'm':
			p.mach = 1;
			break;
		case 'h':
			usage();
			exit(1);
			break;
		case 'c':
			p.class = strdup(optarg);
			break;
		case 'd':
			p.device = strdup(optarg);
			break;
		default:
			phelp++;
		}
	}
	if (phelp) {
		usage();
		exit(1);
	}
	argc -= optind;
	argv += optind;
	if (p.class) {
		if (!(n = read_class(devs, p.class)))
			fail(-1, "Failed to read any devices of class '%s'.\n", p.class);
	} else {
		if (!(n = read_devices(devs)))
			fail(-1, "Failed to read any devices.\n");
	}
	devs[n] = NULL;
	if (p.list) {
		return list_devices(devs);
	}
	dev_name = p.device ? p.device : default_device;
	if (argc == 0)
		p.operation = GET;
	else switch (argv[0][0]) {
	case 'm':
		p.operation = MAX; break;
	case 's':
		p.operation = SET; break;
	default:
	case 'g':
		p.operation = GET; break;
	}
	argc--;
	argv++;
	if (p.operation == SET && argc == 0)
		fail(-1, "You need to provide a value to set.\n");
	if (p.operation == SET && parse_value(&p.val, argv[0]))
		fail(-1, "Invalid value given");
	if (!(dev = find_device(devs, dev_name)))
		fail(-1, "Device '%s' not found.\n", dev_name);
	if (p.operation == SET && !p.pretend && geteuid())
		fail(EPERM, "You need to run this program as root to be able to modify values!\n");
	return apply_operation(dev, p.operation, &p.val);
}

int apply_operation(struct device *dev, unsigned int operation, struct value *val) {
	int retval = 0;
	switch (operation) {
	case GET:
		return print_device(dev);
	case MAX:
		fprintf(stdout, "%d\n", dev->max_brightness);
		return 0;
	case SET:
		apply_value(dev, val);
		if (!p.pretend)
			if (write_device(dev))
				goto fail;
		if (!p.quiet) {
			if (!p.mach)
				fprintf(stdout, "Updated device '%s':\n", dev->id);
			return print_device(dev);
		}
	fail:
	default:
		return retval;
	}
}

int parse_value(struct value *val, char *str) {
	long n;
	char c;
	char *buf;
	errno = 0;
	n = strtol(str, &buf, 10);
	if (errno || buf == str)
		return -1;
	val->val = labs(n);
	val->v_type = ABSOLUTE;
	val->d_type = DIRECT;
	val->sign = PLUS;
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
	return 0;
}

struct device *find_device(struct device **devs, char *name) {
	struct device *dev;
	while ((dev = *(devs++)))
		if (!strcmp(dev->id, name))
			return dev;
	return NULL;
}

int list_devices(struct device **devs) {
	struct device *dev;
	if (!p.mach)
		fprintf(stdout, "Available devices:\n");
	while ((dev = *(devs++)))
		print_device(dev);
	return 0;
}

int print_device(struct device *dev) {
	char *format = p.mach ? "%s,%s,%d,%d%%,%d\n": 
		"Device '%s' of class '%s':\n\tCurrent brightness: %d (%d%%)\n\tMax brightness: %d\n\n";
	fprintf(stdout, format,
		dev->id, dev->class,
		dev->curr_brightness,
		(int) (100.0 * dev->curr_brightness / dev-> max_brightness),
		dev->max_brightness);
	return 0;
}

void apply_value(struct device *d, struct value *val) {
	long new, mod = labs(val->v_type == ABSOLUTE ? 
			val->val : val->val / 100.0 * d->max_brightness);
	if (val->d_type == DIRECT) {
		new = mod > d->max_brightness ? d->max_brightness: mod;
		goto apply;
		}
	mod *= val->sign == PLUS ? 1 : -1;
	new = d->curr_brightness + mod;
	if (new < 0)
		new = 0;
	if (new > d->max_brightness)
		new = d->max_brightness;
apply:
	d->curr_brightness = new;
}

int write_device(struct device *d) {
	FILE *f;
	char c[16];
	size_t s = sprintf(c, "%u", d->curr_brightness);
	errno = 0;
	if (!s) {
		errno = -1;
		goto fail;
	}
	if ((f = fopen(dir_child(device_path(d), "brightness"), "w"))) {
		if (fwrite(c, 1, s + 1, f) < s + 1)
			goto fail;
	} else
		goto fail;
	errno = 0;
fail:
	if (errno)
		perror("Error writing device");
	return errno;
}

int read_device(struct device *d, char *class, char *id) {
	DIR *dirp;
	FILE *f;
	char *dev_path;
	int done_read = 0;
	struct dirent *ent;
	d->class = strdup(class);
	d->id = strdup(id);
	dev_path = device_path(d);
	if (!(dirp = opendir(dev_path)))
		goto fail;
	while ((ent = readdir(dirp))) {
		if (!strcmp(ent->d_name, ".") && !strcmp(ent->d_name, ".."))
			continue;
		if (!strcmp(ent->d_name, "brightness")) {
			if ((f = fopen(dir_child(dev_path, ent->d_name), "r"))) {
				done_read += fscanf(f, "%u", &d->curr_brightness);
				fclose(f);
			} else
				goto fail;
		}
		if (!strcmp(ent->d_name, "max_brightness")) {
			if ((f = fopen(dir_child(dev_path, ent->d_name), "r"))) {
				done_read += fscanf(f, "%u", &d->max_brightness);
				fclose(f);
			} else
				goto fail;
		}
	}
	errno = 0;
fail:
	closedir(dirp);
	if (!errno && !done_read)
		errno = -1;
	if (errno)
		perror("Error reading device");
	return errno;
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
		if (read_device(dev, class, ent->d_name))
			continue;
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


char *cat_with(char c, ...) {
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
		if ((curr = va_arg(va, char*)))
			strcat(buf, split);
	}
	return buf;
}

char *dir_child(char *parent, char *child) {
	return cat_with('/', parent, child, NULL);
}

char *device_path(struct device *dev) {
	return cat_with('/', path, dev->class, dev->id, NULL);
}

char *class_path(char *class) {
	return dir_child(path, class);
}

void fail(int errcode, char *err_msg, ...) {
	va_list va;
	va_start(va, err_msg);
	vfprintf(stderr, err_msg, va);
	exit(errcode);
	va_end(va);
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
  -d, --device=DEVICE\t\tspecify device name.\n\
  -c, --class=CLASS\t\tspecify device class.\n\
  -h, --help\t\t\tprint this help.\n\
\n\
Operations:\n\
  g, get\t\t\tget current brightness of the device.\n\
  m, max\t\t\tget maximum brightness of the device.\n\
  s, set VALUE\t\t\tset brightness of the device.\n\
\n");
}

