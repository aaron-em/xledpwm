/*
 * xledpwm: Control keyboard LED brightness via pulse-width modulation
 * 
 * Arguments:
 *         --display, -d: X display to use (default :0.0)
 *         --led, -l: LED to control (required; takes numeric arguments as xset)
 *         --brightness, -b: Brightness level (1-100, not with -f)
 *         --fade, -f: Fade LED in and out (not with -b)
 *
 * Slightly based on xset 1.2.1, which is copyright 1998 The Open Group.
 * Provided as-is; if it breaks, you get to keep both pieces.
 *
 * Some older 2.6 kernels have a bug in which the USB HID driver's control queue is
 * shorter than it really should be.
 * This program sends a lot of commands to the keyboard.
 * If your kernel has the bug and you use a USB keyboard, this program will probably
 * tickle the bug; you'll see "...control queue full" messages on the console, and will
 * no longer be able to control any keyboard LEDs by any means. (But you can still type.)
 * Unplugging the keyboard will clear the stuffed queue. Upgrading your kernel will solve
 * the problem.
 *
 * This is a proof of concept, meaning it's not all that good. There's not much difference
 * visible in the lower brightness levels, because timing resolution at microsecond levels
 * kind of sucks; if I shorten the duty cycle any further, the result is not lower bright-
 * ness but perceptible flicker, which definitely sucks.
 *
 * If you have reliable nanosecond-level timing, it will no doubt improve this program a
 * great deal; if the main loop were 20us wide instead of 20ms as now, you'd probably be
 * able to dim the LED to imperceptibility without showing any flicker.
 *
 * As things are, there's still a bit of flicker in the fade mode which I haven't been
 * able to expunge; this is probably less to do with any timing issues than with the fact
 * that I kinda suck at math.
 *
 * Sadly, I don't have root on this box, so I can't test that out for myself; at nice 0,
 * even microsecond-level timing isn't all that reliable. (I'm not sure whether it'd be
 * any better at nice -20; you might need a real-time kernel or something. I'm not really
 * a Linux or a C hacker, so I don't really know.)
 *
 * Hacked together 4/11-12/2013 by Aaron (me@aaron-miller.me). Public domain.
 * Share and enjoy; if you do something cool with this, I'd love to hear about it.
 * 
 */

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <X11/Xos.h>
#include <X11/Xfuncs.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>

#define ON   1
#define OFF  0
#define ALL -1

#define TRUE  1
#define FALSE 0

void cleanup(int sig);

static char* progName;
Display *dpy;
int led = -1;

static void
set_led(Display *dpy, int led, int led_mode)
{
  XKeyboardControl values;

  values.led_mode = led_mode;
  if (led != ALL) {
    values.led = led;
    XChangeKeyboardControl(dpy, KBLed | KBLedMode, &values);
  } else {
    XChangeKeyboardControl(dpy, KBLedMode, &values);
  }
  return;
}

static int
is_number(char *arg, int maximum)
{
  register char *p;

  if (arg[0] == '-' && arg[1] == '1' && arg[2] == '\0')
    return (1);
  for (p = arg; isdigit(*p); p++) ;
  if (*p || atoi(arg) > maximum)
    return (0);
  return (1);
}

void cleanup(int sig) {
  set_led(dpy, led, OFF);
  XCloseDisplay(dpy);
  exit(sig);
};

int main(int argc, char *argv[]) {
  char *disp = NULL;
  double hz;
  double cycle = -1;
  int usec_cycle, usec_on, usec_off;
  float factor = 1.25;
  int tick_count = 0;
  int fade = -1;

  register char *arg;
  register int i;

  progName = argv[0];

  signal(SIGINT, cleanup);

  hz = 500;
  usec_cycle = (int) ( (1/hz) * pow(10,7) );

  /* Parse arguments */
  for (i = 1; i < argc; i++) {
    arg = argv[i];
    
    // were we asked for usage information?
    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0 || strcmp(arg, "-?") == 0) {
      printf("%s: light a keyboard LED at controllable brightness via PWM\nArguments:\n\t--display, -d: X display (default :0.0)\n\t--led, -l: LED to control (required; takes numeric arguments as xset)\n\t--brightness, -b: Brightness level (1-100, not with -f)\n\t--fade, -f: Fade LED in and out (not with -b)\n",
             progName);
      exit(0);
    }

    // which display are we using?
    else if (strcmp(arg, "--display") == 0 || strcmp(arg, "-d") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "%s: missing argument to %s\n",
                progName, arg);
        exit(1);
      };
      disp = argv[i];
    }

    // which LED are we using?
    else if (strcmp(arg, "--led") == 0 || strcmp(arg, "-l") == 0) {
      if (++i >= argc) {
        fprintf(stderr, "%s: missing argument to %s\n",
                progName, arg);
        exit(1);
      }
      if (is_number(argv[i], 32) && atoi(argv[i]) > 0) {
        led = atoi(argv[i]);
      }
      else {
        fprintf(stderr, "%s: invalid argument '%s' for %s\n",
                progName, argv[i], arg);
        exit(1);
      };
    }

    // what's our duty cycle? (i.e., for what percentage of the time shall the LED be on?)
    // (we call the argument 'brightness' because that's the effect it will have)
    else if (strcmp(arg, "--brightness") == 0 || strcmp(arg, "-b") == 0) {
      if (fade != -1) {
        fprintf(stderr, "%s: can't specify both -b and -f\n",
                progName);
        exit(1);
      };
      if (++i >= argc) {
        fprintf(stderr, "%s: missing argument to %s\n",
                progName, arg);
        exit(1);
      };
      if (strtod(argv[i], NULL) >= 1.0 && strtod(argv[i], NULL) <= 100.0) {
        cycle = usec_cycle * (strtod(argv[i], NULL) / 100);
        fade = FALSE;
      }
      else {
        fprintf(stderr, "%s: invalid argument '%s' for %s\n",
                progName, argv[i], arg);
        exit(1);
      };
    }

    // should we fade the LED up and down, or just use a fixed brightness?
    else if (strcmp(arg, "--fade") == 0 || strcmp(arg, "-f") == 0) {
      if (cycle != -1) {
        fprintf(stderr, "%s: can't specify both -b and -f\n",
                progName);
        exit(1);
      };
      fade = TRUE;
      cycle = 200;
    }
    
    else {
      fprintf(stderr, "%s: unrecognized argument %s\n",
              progName, arg);
      exit(1);
    };
  };
  /* Done with that, thank God */

  // Did we get an LED number to twiddle?
  if (led == -1) {
    fprintf(stderr, "%s: '--led (or -l) <num>' argument is required\n",
            progName);
    exit(1);
  };

  if (fade == -1 && cycle == -1) {
    fprintf(stderr, "%s: one of '-f' or '-b' is required\n",
            progName);
    exit(1);
  };

  // Can we open the display?
  dpy = XOpenDisplay(disp);
  if (dpy == NULL) {
    fprintf(stderr, "%s: unable to open display '%s'\n",
            progName, XDisplayName(disp));
    exit(1);
  };

  while (1) {

    tick_count++;

    if (fade == TRUE) {
      // FIXME this is awful
      if (tick_count >= (hz/200)) {
        tick_count = 0;
        if (cycle >= usec_cycle) {
          factor = 0.75;
        };
        if (cycle <= (usec_cycle/100)) {
          factor = 1.25;
        };
        cycle *= factor;
        if (cycle < (usec_cycle/100)) { cycle = (usec_cycle/100); };
        if (cycle > usec_cycle) { cycle = usec_cycle; };
      };
    };
    
    usec_on  = (int) cycle;
    usec_off = (int) (usec_cycle - usec_on);
    
    set_led(dpy, led, ON);
    XFlush(dpy);
    usleep(usec_on);

    set_led(dpy, led, OFF);
    XFlush(dpy);
    usleep(usec_off);
  };
  
  cleanup(0);
}
