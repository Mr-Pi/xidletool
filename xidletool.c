/*

xidletool, a program that prints the idle user time to stdout. You can
optionally set a target time, in milliseconds. If reached, the program will
exit.

Based on xprintidle, https://github.com/pmaia/xprintidle-plus

Copyright (c) 2005, 2008 Magnus Henoch <henoch@dtek.chalmers.se>
Copyright (c) 2006, 2007 by Danny Kukawka
                         <dkukawka@suse.de>, <danny.kukawka@web.de>
Copyright (c) 2008 Eivind Magnus Hvidevold <hvidevold@gmail.com>
Copyright (c) 2014 Alex Alexander
                   <wired@gentoo.org> <alex.alexander@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the
Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

The function workaroundCreepyXServer was adapted from kpowersave-0.7.3 by
Eivind Magnus Hvidevold <hvidevold@gmail.com>. kpowersave is licensed under
the GNU GPL, version 2 _only_.

*/

#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

void usage(char *name);
unsigned long workaroundCreepyXServer(Display *dpy, unsigned long _idleTime );
static void signal_callback_handler(int sig, siginfo_t *siginfo, void *context);

Display *dpy;

int main(int argc, char *argv[])
{
	XScreenSaverInfo ssi;
//	Display *dpy;
	int event_basep, error_basep;

	bool verbose = false;
	bool quiet = false;
	long target = 0;
	unsigned long interval = 1000000;

	int c = 0;
	while ((c = getopt (argc, argv, "svqt:i:")) != -1)
		switch (c)
			{
			case 's': //just print idleTime
				target = -1;
				break;
			case 'v': //verbose
				verbose = true;
				break;
			case 'q': //be quiet (only useful if -t specified)
				quiet = 1;
				break;
			case 't': //timeout
				target = atoi(optarg);
				break;
			case 'i':
				interval = atoi(optarg) * 1000;
				break;
			case '?':
				if (optopt == 't' || optopt == 'c')
					fprintf (stderr, "Option -%c requires an argument.\n", optopt);
				else if (isprint (optopt))
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf (stderr,
									 "Unknown option character `\\x%x'.\n",
									 optopt);
				usage(argv[0]);
				return 1;
			default:
				usage(argv[0]);
			}

	if (!(target >= -1 && interval > 0)) {
		usage(argv[0]);
		return 1;
	}

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "couldn't open display\n");
		return 1;
	}

	struct sigaction act;
	memset (&act, '\0', sizeof(act));
 
	/* Use the sa_sigaction field because the handles has two additional parameters */
	act.sa_sigaction = &signal_callback_handler;

	/* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
	act.sa_flags = SA_SIGINFO;

	// Register signal and signal handler
	if (sigaction(SIGTERM, &act, NULL) < 0) {
		perror ("sigaction");
		return 1;
	}

	setlinebuf(stdout);

	unsigned long current = 0;
	while (target <= 0 || current < target) {
		if (target != -1)
			usleep(interval);

		if (!XScreenSaverQueryExtension(dpy, &event_basep, &error_basep)) {
			fprintf(stderr, "screen saver extension not supported\n");
			return 1;
		}

		if (!XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), &ssi)) {
			fprintf(stderr, "couldn't query screen saver info\n");
			return 1;
		}

		current = workaroundCreepyXServer(dpy, ssi.idle);
		if (target == -1) {
			printf("%lu\n", current);
			return 0;
		}
		if (!quiet)
			if (verbose)
				printf("%lu - %lu\n", time(NULL), current);
			else
				printf("%lu\n", current);
	}
	if (!quiet && target > 0)
		printf("Reached idle target: %lu | timestamp: %lu\n", current, time(NULL));

	return 0;
}

static void signal_callback_handler(int sig, siginfo_t *siginfo, void *context) {
	XCloseDisplay(dpy);
}

void usage(char *name)
{
	fprintf(stderr,
		"Usage:\n"
		"%s [-s] [-t target] [-i interval] [-q] [-v]\n"
		"  -s\n"
		"       print the current idle time and exit\n"
		"  -i interval (in milliseconds)\n"
		"       check idle time every <interval> milliseconds\n"
		"  -t target (in milliseconds)\n"
		"       run until system has been idle for target milliseconds\n"
		"  -q\n"
		"       when target is reached, don't print anything, just exit\n"
		"  -v\n"
		"       print a message on each interval in target mode\n"
		"Note that -s and -t are mutually exclusive, only the last one matters.\n"
		"By default, %s runs indefinitely with an interval of 1000 milliseconds.\n"
		"The user's idle time in milliseconds is printed on stdout.\n",
		name, name);
}

/*!
 * This function works around an XServer idleTime bug in the
 * XScreenSaverExtension if dpms is running. In this case the current
 * dpms-state time is always subtracted from the current idletime.
 * This means: XScreenSaverInfo->idle is not the time since the last
 * user activity, as descriped in the header file of the extension.
 * This result in SUSE bug # and sf.net bug #. The bug in the XServer itself
 * is reported at https://bugs.freedesktop.org/buglist.cgi?quicksearch=6439.
 *
 * Workaround: Check if if XServer is in a dpms state, check the 
 *             current timeout for this state and add this value to 
 *             the current idle time and return.
 *
 * \param _idleTime a unsigned long value with the current idletime from
 *                  XScreenSaverInfo->idle
 * \return a unsigned long with the corrected idletime
 */
unsigned long workaroundCreepyXServer(Display *dpy, unsigned long _idleTime ){
	int dummy;
	CARD16 standby, suspend, off;
	CARD16 state;
	BOOL onoff;

	if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
		if (DPMSCapable(dpy)) {
			DPMSGetTimeouts(dpy, &standby, &suspend, &off);
			DPMSInfo(dpy, &state, &onoff);

			if (onoff) {
				switch (state) {
					case DPMSModeStandby:
						/* this check is a littlebit paranoid, but be sure */
						if (_idleTime < (unsigned) (standby * 1000))
							_idleTime += (standby * 1000);
						break;
					case DPMSModeSuspend:
						if (_idleTime < (unsigned) ((suspend + standby) * 1000))
							_idleTime += ((suspend + standby) * 1000);
						break;
					case DPMSModeOff:
						if (_idleTime < (unsigned) ((off + suspend + standby) * 1000))
							_idleTime += ((off + suspend + standby) * 1000);
						break;
					case DPMSModeOn:
					default:
						break;
				}
			}
		}
	}

	return _idleTime;
}
