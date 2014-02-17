/*
 * Copyright (c) 2012 Yann Cantin <yann.cantin@laposte.net>
 *
 * based on xinput_calibrator by Tias Guns
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

/*
 * signal (SIGALRM) based "event loop"
 * Xlib + signal + oop madness
 */

#include "gui/x11.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>

#include <string>
#include <stdexcept>

// X
#ifdef HAVE_X11_XRANDR
#include <X11/extensions/Xrandr.h> // support for multi-head setups
#endif

/// look'n fell
// Timeout parameters
const int time_step = 100;  // in milliseconds
const int max_time = 15000; // in milliseconds, 5000 = 5 sec

// Point appereance
const int cross_lines = 25;
const int cross_circle = 10;

// Clock appereance
const int clock_radius = 50;
const int clock_line_width = 10;

// Text printed on screen
const int help_lines = 4;
const std::string help_text[help_lines] = {
    "eBeam Calibration",
    "Press the point in red with the stylus.",
    "",
    "(To abort, press any key or wait)"
};

/// static
// Keep in sync with x11.hpp
const char* GuiCalibratorX11::colors[GuiCalibratorX11::NUM_COLORS] =
    {"BLACK", "WHITE", "GRAY", "DIMGRAY", "RED", "DARKGREEN"};

// signal handler
void sigalarm_handler(int num)
{
    if (num == SIGALRM) {
        GuiCalibratorX11::timer_signal();
    }
}

// verbose
bool GuiCalibratorX11::verbose = false;

// in final step ?
bool GuiCalibratorX11::final_step = false;

// keep alive
bool GuiCalibratorX11::is_running = false;

// singleton instance
GuiCalibratorX11* GuiCalibratorX11::instance = NULL;

GuiCalibratorX11::GuiCalibratorX11(Calibrator* calibrator0)
  : calibrator(calibrator0),
    display_width(-1),
    display_height(-1),
    raw_X(0),
    raw_Y(0),
    time_elapsed(0)
{
    is_running = true;

    verbose = calibrator->verbose;
    
    // active zone
    min_x = calibrator->get_min_x();
    min_y = calibrator->get_min_y();
    max_x = calibrator->get_max_x();
    max_y = calibrator->get_max_y();
       
    /*
     * Check server
     */
    display = XOpenDisplay(NULL);
    if (display == NULL) {
        throw std::runtime_error("Unable to connect to X server.");
    }

    screen_num = DefaultScreen(display);

    // Is XInput Extension available ?
    int event, error;
    if (!XQueryExtension(display, "XInputExtension",
                                  &xi_opcode, &event, &error)) {
        throw std::runtime_error("X Input extension not available.");
    }

    // Which version of XI2? We need 2.0
    int major = 2, minor = 0;
    if (XIQueryVersion(display, &major, &minor) == BadRequest) {
        throw std::runtime_error("XI2 not available.");
    }

    if (verbose)
        fprintf(stderr, "XI2 available. Server supports %d.%d.\n",
                        major, minor);

    // Load font and get font information structure
    font_info = XLoadQueryFont(display, "9x15");
    if (font_info == NULL) {
        // fall back to native font
        font_info = XLoadQueryFont(display, "fixed");
        if (font_info == NULL) {
            XCloseDisplay(display);
            throw std::runtime_error("Unable to open font");
        }
    }

    setup_zone();
    
    if (verbose) {
        fprintf(stderr, "Calibrating '%s' (%i)\n",
                        calibrator->get_device_name(),
                        (int) calibrator->get_device_id());
        if (calibrator->is_zoned()) {
	    fprintf(stderr, "  with (%i %i %i %i) active zone.\n",
                    min_x, min_y, max_x, max_y);
        }
    }

    /*
     * reseting device and X calibration
     */
    if (!( calibrator->reset_ebeam_calibration() &&
           calibrator->reset_evdev_calibration()    ))
        throw std::runtime_error("Unable to reset calibration.");


    /*
     * Setup calibration window
     */
    XSetWindowAttributes attributes;
    attributes.override_redirect = true;

    // select X events
    attributes.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;

    win = XCreateWindow(display, RootWindow(display, screen_num),
                                 0, 0, display_width, display_height,
                                 CopyFromParent,
                                 CopyFromParent,
                                 InputOutput,
                                 CopyFromParent,
                                 CWOverrideRedirect | CWEventMask,
                                 &attributes);

    XMapWindow(display, win);

    // hide cursor
    Cursor invisibleCursor;
    Pixmap bitmapNoData;
    XColor black;
    static char noData[] = { 0,0,0,0,0,0,0,0 };
    black.red = black.green = black.blue = 0;

    bitmapNoData = XCreateBitmapFromData(display, win, noData, 8, 8);
    invisibleCursor = XCreatePixmapCursor(display, bitmapNoData,
                                                   bitmapNoData,
                                                   &black,
                                                   &black,
                                                   0, 0);
    XDefineCursor(display,win, invisibleCursor);
    XFreeCursor(display, invisibleCursor);

    // select XI2 events
    XIEventMask mask;
    mask.mask_len = XIMaskLen(XI_LASTEVENT);
    mask.mask = (unsigned char*) calloc(mask.mask_len, sizeof(char));

    // Select keypress from master device
    mask.deviceid = XIAllMasterDevices;
    memset(mask.mask, 0, mask.mask_len);
    XISetMask(mask.mask, XI_KeyPress);
    XISelectEvents(display, win, &mask, 1);

    // grab master keyboard
    XIGrabDevice(display, 3, win,
                             CurrentTime,
                             None,
                             GrabModeAsync,
                             GrabModeAsync,
                             False,
                             &mask);

    // Select raw events from eBeam device
    mask.deviceid = calibrator->get_device_id();
    memset(mask.mask, 0, mask.mask_len);
    XISetMask(mask.mask, XI_RawButtonPress);
    XISetMask(mask.mask, XI_RawMotion);
    XISelectEvents(display, RootWindow(display, screen_num), &mask, 1);

    free(mask.mask);

    // get colors
    Colormap colormap = DefaultColormap(display, screen_num);
    XColor color;
    for (int i = 0; i != NUM_COLORS; i++) {
        XParseColor(display, colormap, colors[i], &color);
        XAllocColor(display, colormap, &color);
        pixel[i] = color.pixel;
    }

    // background
    XSetWindowBackground(display, win, pixel[BLACK]);
    XClearWindow(display, win);

    // Graphic context
    gc = XCreateGC(display, win, 0, NULL);
    XSetFont(display, gc, font_info->fid);


    /*
     * Setup timer : clock animation & event loop
     */
    signal(SIGALRM, sigalarm_handler);
    struct itimerval timer;
    timer.it_value.tv_sec = time_step/1000;
    timer.it_value.tv_usec = (time_step % 1000) * 1000;
    timer.it_interval = timer.it_value;
    // here we go...
    setitimer(ITIMER_REAL, &timer, NULL);
}

GuiCalibratorX11::~GuiCalibratorX11()
{
    // ungrab keyboard
    XIUngrabDevice(display, 3, CurrentTime);

    XFreeGC(display, gc);
    XCloseDisplay(display);
}

///
/// static members
///

void GuiCalibratorX11::make_instance(Calibrator* w)
{
    instance = new GuiCalibratorX11(w);
}

void GuiCalibratorX11::destroy_instance()
{
	delete instance;
}

void GuiCalibratorX11::timer_signal()
{
    if (instance != NULL) {
        // check timeout, update clock
        instance->clock_tick();

        // process events
        XEvent event;

        while (XPending(instance->display)) {      // don't get stuck waiting
            XNextEvent(instance->display, &event);

            if (event.type == Expose) {
                if (event.xexpose.count != 0) // only catch last one
                        continue;
                instance->on_expose_event();
                continue;
            }

            XGenericEventCookie *cookie = &event.xcookie;

            if (cookie->type != GenericEvent ||
                cookie->extension != instance->xi_opcode ||
                !XGetEventData(instance->display, cookie))
                continue;

            switch (cookie->evtype) {
                case XI_RawMotion:
                    instance->on_motion_event((XIRawEvent *) cookie->data);
                    break;

                case XI_RawButtonPress:
                    instance->on_button_event((XIRawEvent *) cookie->data);
                    break;

                case XI_KeyPress:
                    XFreeEventData(instance->display, cookie);
                    //exit(1);
		    is_running = false;
                    break;

                default:
                    if (instance->verbose)
                        fprintf(stderr, "Uncatched XI2 event.\n");
                    break;
            }

            XFreeEventData(instance->display, cookie);
        }
    }
}

///
/// regular members
///

void GuiCalibratorX11::setup_zone() {
    int width;
    int height;

#ifdef HAVE_X11_XRANDR
    int nsizes;

    XRRScreenSize* randrsize = XRRSizes(display, screen_num, &nsizes);
    if (nsizes != 0) {
        Rotation screenrot = 0;
        XRRRotations(display, screen_num, &screenrot);
        bool rot = screenrot & RR_Rotate_90 || screenrot & RR_Rotate_270;
        width = rot ? randrsize->height : randrsize->width;
        height = rot ? randrsize->width : randrsize->height;
    } else {
        width = DisplayWidth(display, screen_num);
        height = DisplayHeight(display, screen_num);
    }
#else
    width = DisplayWidth(display, screen_num);
    height = DisplayHeight(display, screen_num);
#endif

    if (display_width == width && display_height == height)
        return; // nothing to do

    display_width = width;
    display_height = height;

    // TARGET
    // Compute absolute circle centers
    const int delta_x = (max_x - min_x +1)/NUM_BLOCKS;
    const int delta_y = (max_y - min_y +1)/NUM_BLOCKS;

    // upper left
    target_x[UL] = min_x + delta_x;
    target_y[UL] = min_y + delta_y;

    // lower left
    target_x[LL] = min_x + delta_x;
    target_y[LL] = max_y - delta_y;

    // upper right
    target_x[UR] = max_x - delta_x;
    target_y[UR] = min_y + delta_y;

    //lower right
    target_x[LR] = max_x - delta_x;
    target_y[LR] = max_y - delta_y;

    // reset calibration data
    calibrator->reset_tuples();
}

/// draw the window
void GuiCalibratorX11::redraw()
{
    int w;

    // check display size
    setup_zone();

    /*
     * Print the text
     */
    int text_height = font_info->ascent + font_info->descent;
    int text_width = -1;

    // max width of lines
    for (int i = 0; i != help_lines; i++) {
        text_width = std::max(text_width, XTextWidth(font_info,
                                                     help_text[i].c_str(),
                                                     help_text[i].length()));
    }

    int x = min_x + ((max_x - min_x +1) - text_width) / 2;
    int y = min_y + ((max_y - min_y +1) - text_height) / 2 - 60;

    // active zone background
    XSetForeground(display, gc, pixel[GRAY]);
    XFillRectangle(display, win, gc, min_x,
                                     min_y,
                                     max_x - min_x +1,
                                     max_y - min_y +1);

    
    XSetForeground(display, gc, pixel[BLACK]);
    XSetLineAttributes(display, gc, 2,
                                    LineSolid,
                                    CapRound,
                                    JoinRound);

    XDrawRectangle(display, win, gc, x - 10,
                                     y - (help_lines*text_height) - 10,
                                     text_width + 20,
                                     (help_lines*text_height) + 20);

    // Print help lines
    y -= 3;
    for (int i = help_lines-1; i != -1; i--) {
        w = XTextWidth(font_info, help_text[i].c_str(), help_text[i].length());
        XDrawString(display, win, gc, x + (text_width-w)/2,
                                      y,
                                      help_text[i].c_str(),
                                      help_text[i].length());
        y -= text_height;
    }

    /*
     * Draw the targets
     */
    if (!final_step) {
	for (int i = 0; i <= calibrator->get_numclicks(); i++) {

		// set color: already clicked or not
		if (i < calibrator->get_numclicks())
		XSetForeground(display, gc, pixel[WHITE]);
		else
		XSetForeground(display, gc, pixel[RED]);

		XSetLineAttributes(display, gc, 1, LineSolid, CapRound, JoinRound);

		XDrawLine(display, win, gc, target_x[i] - cross_lines,
					target_y[i],
					target_x[i] + cross_lines,
					target_y[i]);

		XDrawLine(display, win, gc, target_x[i],
					target_y[i] - cross_lines,
					target_x[i],
					target_y[i] + cross_lines);

		XSetLineAttributes(display, gc, 2, LineSolid, CapRound, JoinRound);

		XDrawArc(display, win, gc, target_x[i] - cross_circle,
					target_y[i] - cross_circle,
					(2 * cross_circle),
					(2 * cross_circle),
					0, 360 * 64);
	}
    }
    /*
     * Draw the clock background
     */
    XSetForeground(display, gc, pixel[DIMGRAY]);
    XSetLineAttributes(display, gc, 0,
                                    LineSolid,
                                    CapRound,
                                    JoinRound);
    XFillArc(display, win, gc, min_x + ((max_x - min_x +1) - clock_radius)/2,
                               min_y + ((max_y - min_y +1) - clock_radius)/2,
                               clock_radius,
                               clock_radius,
                               0, 360 * 64);
}

void GuiCalibratorX11::draw_message(const char* msg, const int color)
{
    int text_height = font_info->ascent + font_info->descent;
    int text_width = XTextWidth(font_info, msg, strlen(msg));

    int x = min_x + ((max_x - min_x +1) - text_width) / 2;
    int y = min_y + ((max_y - min_y +1) - text_height) / 2 + clock_radius + 60;
    
    redraw();

    XSetForeground(display, gc, pixel[color]);
    XSetLineAttributes(display, gc, 2,
                                    LineSolid,
                                    CapRound,
                                    JoinRound);

    XDrawRectangle(display, win, gc, x - 10,
                                     y - text_height - 10,
                                     text_width + 20,
                                     text_height + 25);

    XDrawString(display, win, gc, x, y, msg, strlen(msg));
}

/// events
void GuiCalibratorX11::on_expose_event()
{
    redraw();
}

void GuiCalibratorX11::clock_tick()
{
    time_elapsed += time_step;
    if (time_elapsed > max_time) {
	is_running = false;
	return;
	//exit(1);
    }

    // Update clock
    XSetForeground(display, gc, pixel[BLACK]);
    XSetLineAttributes(display, gc, clock_line_width,
                                    LineSolid,
                                    CapButt,
                                    JoinMiter);

    int clock_diameter = clock_radius - clock_line_width;
    double clock_time = ((double)time_elapsed/(double)max_time);
    XDrawArc(display, win, gc, min_x + ((max_x - min_x +1)  - clock_diameter)/2,
                               min_y + ((max_y - min_y +1) - clock_diameter)/2,
                               clock_diameter,
                               clock_diameter,
                               90*64,
                               clock_time * -360 * 64);
}

void GuiCalibratorX11::on_motion_event(XIRawEvent *event)
{
    double *raw_value = event->raw_values;

    // Only store raw values if not in final step and both are present
    if (!final_step			      &&
	XIMaskIsSet(event->valuators.mask, 0) &&   // X
        XIMaskIsSet(event->valuators.mask, 1)) {   // Y
        raw_X = (int) *raw_value;
        raw_value++;
        raw_Y = (int) *raw_value;
    }
}

void GuiCalibratorX11::on_button_event(XIRawEvent *event)
{
    // final step : wait for a click and leave
    if (final_step) {
        is_running = false;
	return;
    }
    
    bool success;
    int i = calibrator->get_numclicks();

    // Clear window, maybe a bit overdone, but easiest atm.
    // (goal is to clear possible message and other clicks)
    XClearWindow(display, win);

    // reset timeout
    time_elapsed = 0;

    success = calibrator->add_click(raw_X, raw_Y, target_x[i], target_y[i]);

    if (!success) {
        draw_message("Double click detected, click on the next point in red.", BLACK);
        return;
    }

    // Are we done yet?
    if (calibrator->get_numclicks() == NUM_POINTS) {
	final_step = true;
        success = calibrator->finish();

        if (success) {
	    draw_message("Calibration complete.", DARKGREEN);
	    return;
        } else {
	    draw_message("Calibration failed.", RED);
            if (verbose)
                fprintf(stderr, "ERROR: Calibration failed.\n");
	   return;
        }       
    }

    redraw();
}

