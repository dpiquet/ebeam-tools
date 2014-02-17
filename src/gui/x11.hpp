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

#ifndef GUI_CALIBRATOR_X11
#define GUI_CALIBRATOR_X11

#include "calibrator.hpp"

#include <X11/extensions/XInput2.h>

/*
 * Number of blocks. We partition the screen into 'num_blocks' x 'num_blocks'
 * rectangles of equal size. We then ask the user to press points that are
 * located at the corner closes to the center of the four blocks in the corners
 * of the screen. The following ascii art illustrates the situation.
 * We partition the screen into 8 blocks in each direction. We then let the user
 * press the points marked with 'O'.
 *
 *   +--+--+--+--+--+--+--+--+
 *   |  |  |  |  |  |  |  |  |
 *   +--O--+--+--+--+--+--O--+
 *   |  |  |  |  |  |  |  |  |
 *   +--+--+--+--+--+--+--+--+
 *   |  |  |  |  |  |  |  |  |
 *   +--+--+--+--+--+--+--+--+
 *   |  |  |  |  |  |  |  |  |
 *   +--+--+--+--+--+--+--+--+
 *   |  |  |  |  |  |  |  |  |
 *   +--+--+--+--+--+--+--+--+
 *   |  |  |  |  |  |  |  |  |
 *   +--+--+--+--+--+--+--+--+
 *   |  |  |  |  |  |  |  |  |
 *   +--O--+--+--+--+--+--O--+
 *   |  |  |  |  |  |  |  |  |
 *   +--+--+--+--+--+--+--+--+
 */
#define NUM_BLOCKS 8

/*******************************************
 * X11 class for the the calibration GUI
 *******************************************/
class GuiCalibratorX11
{
public:
    // Create singleton instance associated to calibrator w
    static void make_instance(Calibrator* w);

    // Destroy singleton instance
    static void destroy_instance();

    // signal handling : update clock and process events (fake event loop)
    static void timer_signal();

    // Be verbose or not (duplicate calibrator's state)
    static bool verbose;
    
    // display calibration result in gui
    static bool	final_step;

    // keep alive
    static bool is_running;

protected:
    GuiCalibratorX11(Calibrator* w);
    ~GuiCalibratorX11();

    // drawing functions
    void setup_zone();
    void redraw();
    void draw_message(const char* msg, const int color);

    // events functions
    void clock_tick();                        // check timeout, updtate clock
    void on_expose_event();                   // redraw event
    void on_motion_event(XIRawEvent *event);  // motion event
    void on_button_event(XIRawEvent *event);  // button event

    // calibrator
    Calibrator* calibrator;

    // X11 vars
    Display*     display;
    int          xi_opcode; // XI2
    int          screen_num;
    Window       win;
    GC           gc;
    XFontStruct* font_info;
    int          display_width;
    int          display_height;
    
    // active zone
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    // color management
    enum { BLACK=0, WHITE=1, GRAY=2, DIMGRAY=3, RED=4, DARKGREEN=5, NUM_COLORS };
    static const char*  colors[NUM_COLORS];
    unsigned long       pixel[NUM_COLORS];

    // targets
    double      target_x[NUM_POINTS];
    double      target_y[NUM_POINTS];

    // eBeam raw values stored waiting for the button event
    int      raw_X;
    int      raw_Y;

    // clock
    int         time_elapsed;

private:
    // singleton instance
    static GuiCalibratorX11* instance;
};

#endif
