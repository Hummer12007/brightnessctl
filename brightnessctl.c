#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <fnmatch.h>
#include <inttypes.h>
#include <limits.h>
#include <getopt.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ENABLE_SYSTEMD
# include <systemd/sd-bus.h>
#endif

static char *path = "/sys/class";
static char *classes[] = { "backlight", "leds", NULL };

static char *run_dir = "/tmp/brightnessctl";

struct value;
struct device;

enum operation;

static void fail(char *, ...);
static void usage(void);
#define cat_with(...) _cat_with(__VA_ARGS__, NULL)
static char *_cat_with(char, ...);
static char *dir_child(char *, char*);
static char *device_path(struct device *);
static char *dev_run_path(struct device *, char *);
static char *class_path(char *);
static unsigned int calc_value(struct device *, struct value *);
static int apply_operation(struct device *, enum operation, struct value *);
static bool parse_value(struct value *, char *);
static bool do_write_device(struct device *);
static bool read_device(struct device *, char *, char *);
static int read_class(struct device **, char *);
static int read_devices(struct device **);
static void print_device(struct device *);
static void list_devices(struct device **);
static struct device *find_device(struct device **, char *);
static bool save_device_data(struct device *);
static bool restore_device_data(struct device *);
static bool ensure_dir(char *);
static bool ensure_dev_dir(struct device *);
static double lerp(double, double, double);
static double unlerp(double, double, double);
static void anim_reload_target(int sig);
static void animate(struct device *, struct value *);
static void anim_worker(struct device *, struct value *);
static void ctx_dump(struct device *, struct value *);
static void ctx_load(struct device *, struct value *);
#define ensure_run_dir() ensure_dir(run_dir)

#ifdef ENABLE_SYSTEMD
static bool logind_set_brightness(struct device *);
#endif

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
	float val;
	enum value_type v_type;
	enum delta_type d_type;
	enum sign sign;
};

enum operation { INFO, GET, MAX, SET };

struct params {
	char *class;
	char *device;
	struct value val;
	long min;
	enum operation operation;
	bool quiet;
	bool list;
	bool pretend;
	bool mach;
	bool save;
	bool restore;
	float exponent;
	float animation;
};

struct ctx {
	struct value val;
	long min;
	float exponent;
	float animation;
};

static struct params p;

static jmp_buf anim_buf;

static const struct option options[] = {
	{"class", required_argument, NULL, 'c'},
	{"device", required_argument, NULL, 'd'},
	{"help", no_argument, NULL, 'h'},
	{"list", no_argument, NULL, 'l'},
	{"machine-readable", no_argument, NULL, 'm'},
	{"min-value", optional_argument, NULL, 'n'},
	{"exponent", optional_argument, NULL, 'e'},
	{"animate", optional_argument, NULL, 'a'},
	{"quiet", no_argument, NULL, 'q'},
	{"pretend", no_argument, NULL, 'p'},
	{"restore", no_argument, NULL, 'r'},
	{"save", no_argument, NULL, 's'},
	{"version", no_argument, NULL, 'V'},
	{NULL,}
};

static bool (*write_device)(struct device *) = do_write_device;

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
	p.exponent = 1;
	while (1) {
		if ((c = getopt_long(argc, argv, "lqpmn::e::a::srhVc:d:", options, NULL)) < 0)
			break;
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
		case 'n':
			if (optarg)
				p.min = atol(optarg);
			else
				p.min = 1;
			break;
		case 'e':
			if (optarg)
				p.exponent = atof(optarg);
			else
				p.exponent = 4;
			break;
		case 'a':
			if (optarg)
				p.animation = atof(optarg);
			else
				p.animation = 0.2;
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
#ifdef ENABLE_SYSTEMD
			write_device = logind_set_brightness;
#else
			perror("Can't modify brightness");
			fail("\nYou should run this program with root privileges.\n"
				"Alternatively, get write permissions for device files.\n");
#endif
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

int apply_operation(struct device *dev, enum operation operation, struct value *val) {
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
		if (!p.pretend) {
			if (p.animation) {
				if (!fork()) {
					animate(dev, val);
					return 0;
				}
			} else {
				dev->curr_brightness = calc_value(dev, val);
				if (!write_device(dev))
					goto fail;
			}
		}
		if (!p.quiet) {
			if (!p.mach)
				fprintf(stdout, "%s device '%s':\n",
						p.animation ? "Updating" : "Updated",
						dev->id);
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
	char *format = p.mach ? "%s,%s,%d,%d%%,%d\n" :
		"Device '%s' of class '%s':\n\tCurrent brightness: %d (%d%%)\n\tMax brightness: %d\n\n";
	fprintf(stdout, format,
		dev->id, dev->class,
		dev->curr_brightness,
		(int) val_to_percent(dev->curr_brightness, dev, true),
		dev->max_brightness);
}

unsigned int calc_value(struct device *d, struct value *val) {
	long new = d->curr_brightness;
	if (val->d_type == DIRECT) {
		new = val->v_type == ABSOLUTE ? val->val : percent_to_val(val->val, d);
		goto apply;
	}
	long mod = val->val;
	if (val->sign == MINUS)
		mod *= -1;
	if (val->v_type == RELATIVE) {
		mod = percent_to_val(val_to_percent(d->curr_brightness, d, false) + mod, d) - d->curr_brightness;
		if (val->val != 0 && mod == 0)
			mod = val->sign == PLUS ? 1 : -1;
	}
	new += mod;
apply:
	if (new < p.min)
		new = p.min;
	if (new < 0)
		new = 0;
	if (new > d->max_brightness)
		new = d->max_brightness;
	return new;
}

#ifdef ENABLE_SYSTEMD

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
	char *d_path = device_path(d);
	char *b_path = dir_child(d_path, "brightness");
	errno = 0;
	if (s <= 0) {
		errno = EINVAL;
		goto fail;
	}
	if ((f = fopen(b_path, "w"))) {
		if (fwrite(c, 1, s, f) < s)
			goto close;
	} else
		goto fail;
	errno = 0;
close:
	fclose(f);
fail:
	free(d_path);
	free(b_path);
	if (errno)
		perror("Error writing device");
	return !errno;
}

bool read_device(struct device *d, char *class, char *id) {
	DIR *dirp;
	FILE *f;
	char *dev_path = NULL;
	char *ent_path;
	int error = 0;
	struct dirent *ent;
	bool cur;
	if (class && id) {
		d->class = strdup(class);
		d->id = strdup(id);
	}
	dev_path = device_path(d);
	if (!(dirp = opendir(dev_path)))
		goto dfail;
	while ((ent = readdir(dirp))) {
		if (!strcmp(ent->d_name, ".") && !strcmp(ent->d_name, ".."))
			continue;
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

int read_class(struct device **devs, char *class) {
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
		dev = malloc(sizeof(struct device));
		if (!read_device(dev, class, ent->d_name)) {
			free(dev);
			continue;
		}
		devs[cnt++] = dev;
	}
	closedir(dirp);
	free(c_path);
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

struct timespec get_target_time() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	if (p.animation < 0) {
		fprintf(stderr, "Animation duration must be non-negative!");
		exit(-1);
	}
	ts.tv_sec += (int) p.animation;
	ts.tv_nsec += (p.animation - (int) p.animation) * 1e9;
	if (ts.tv_nsec >= 1e9) {
		ts.tv_sec++;
		ts.tv_nsec -= 1e9;
	}
	return ts;
}

uint64_t tousecs(struct timespec ts) {
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

double lerp(double a, double b, double t) {
        return (1 - t) * a + t * b;
}

double unlerp(double a, double b, double v) {
        return (v - a) / (b - a);
}

void anim_reload_target(int sig) {
	(void) sig;
	longjmp(anim_buf, 1);
}

void ctx_load(struct device *dev, struct value *val) {
	struct ctx ctx;
	char *cpath = dev_run_path(dev, "ctx");
	int fd = open(cpath, O_RDONLY);
	read(fd, &ctx, sizeof(struct ctx));
	memcpy(val, &ctx.val, sizeof(struct value));
	p.min = ctx.min, p.exponent = ctx.exponent, p.animation = ctx.animation;
	close(fd);
	free(cpath);
}

void ctx_dump(struct device *dev, struct value *val) {
	char *cpath = dev_run_path(dev, "ctx");
	int fd = open(cpath, O_CREAT | O_WRONLY | O_TRUNC, 0777);
	struct ctx ctx = {.min = p.min, .exponent = p.exponent, .animation = p.animation};
	memcpy(&ctx.val, val, sizeof(struct value));
	write(fd, &ctx, sizeof(struct ctx));
	close(fd);
	free(cpath);
}

void convert_value(struct device *d, unsigned int target, struct value *val) {
	unsigned int cur;
	if (val->d_type == DIRECT)
		return;
	read_device(d, NULL, NULL);
	fprintf(stderr, "In: %s %u -> %u\n", d->id, d->curr_brightness, target);
	fprintf(stderr, "In: %s %f\n", val->v_type == ABSOLUTE ? "A" : "%",
			val->val);
	cur = d->curr_brightness;
	d->curr_brightness = target;
	val->val = val->v_type == ABSOLUTE ? calc_value(d, val) : val_to_percent(calc_value(d, val), d, false);
	val->d_type = DIRECT;
	d->curr_brightness = cur;
	fprintf(stderr, "Out: %s %f\n", val->v_type == ABSOLUTE ? "A" : "%",
			val->val);
}

void animate(struct device *d, struct value *val) {
	char *ppath = dev_run_path(d, "pid");
	int pfd = open(ppath, O_RDONLY);
	int64_t pid;
	if (!ensure_dev_dir(d))
		return; // TODO: handle
launch:
	pfd = open(ppath, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, 0777);
	if (pfd >= 0) {
		pid = getpid();
		write(pfd, &pid, sizeof(int64_t));
		close(pfd);
		anim_worker(d, val);
		unlink(ppath);
		goto cleanup;
	}
	pfd = open(ppath, O_RDONLY);
	read(pfd, &pid, sizeof(int64_t));
	close(pfd);
	ctx_dump(d, val);
	errno = 0;
	if (kill(pid, SIGHUP)) {
		if (errno == ESRCH) {
			unlink(ppath);
			goto launch;
		} else {
			// EPERM, :(
		}
	}
cleanup:
	free(ppath);
	return;
}

void anim_worker(struct device *d, struct value *val) {
	struct timespec target, ts;
	long tl, tr, ct, delta, oldval;
	struct value init, cur, new;
	struct sigaction sn;
	//char *tpath = dev_run_path(d, "target");
	//int tfd;
	unsigned int target_val;

	sigemptyset(&sn.sa_mask);
	sn.sa_flags = 0;
	sn.sa_handler = anim_reload_target;
	sigaction(SIGHUP, &sn, NULL);

	if (setjmp(anim_buf)) {
		ctx_load(d, &new);
		convert_value(d, target_val, &new);
		memcpy(val, &new, sizeof(struct value));
	} else {
		convert_value(d, d->curr_brightness, val);
	}

	target_val = calc_value(d, val);

	memcpy(&init, val, sizeof(struct value));
	init.val = val->d_type == DELTA ? 0 :
		   (val->v_type == ABSOLUTE ? d->curr_brightness :
			    val_to_percent(d->curr_brightness, d, false));
	memcpy(&cur, &init, sizeof(struct value));
	oldval = d->curr_brightness;

	target = get_target_time();
	clock_gettime(CLOCK_MONOTONIC, &ts);
	ct = tl = tousecs(ts), tr = tousecs(target);
	while ((delta = tr - ct) > 0) {
		cur.val = lerp(init.val, val->val, unlerp(tl, tr, ct));
		d->curr_brightness = calc_value(d, &cur);
		if (oldval != d->curr_brightness)
			write_device(d);
		oldval = d->curr_brightness;
		target.tv_sec = 0;
		target.tv_nsec = (delta > 10000 ? 10000 : delta) * 1000;
		nanosleep(&target, NULL);
		clock_gettime(CLOCK_MONOTONIC, &ts);
		ct = tousecs(ts);
	}
	apply_value(d, val);
	write_device(d);
	//free(tpath);
}

bool save_device_data(struct device *dev) {
	char c[16];
	size_t s = sprintf(c, "%u", dev->curr_brightness);
	char *d_path = cat_with('/', run_dir, dev->class, dev->id);
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

char *dev_run_path(struct device *dev, char *obj) {
	char *buf = calloc(1, strlen(obj) + strlen(dev->id) + 3);
	char *s = "__";
	char *ret;
	strcpy(buf, dev->id);
	strcat(buf, s);
	strcat(buf, obj);
	ret = cat_with('/', run_dir, dev->class, buf);
	free(buf);
	return ret;
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
  -e, --exponent[=K]\t\tchanges percentage curve to exponential.\n\
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

