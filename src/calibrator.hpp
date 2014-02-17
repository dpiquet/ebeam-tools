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

#ifndef _calibrator_hpp
#define _calibrator_hpp

#include <X11/Xlib.h>
#include <X11/extensions/XInput.h>

#ifndef SUCCESS
#define SUCCESS 1
#endif
#ifndef FAILURE
#define FAILURE 0
#endif

/*
 * eBeam devices return instable values, defaulting to 16
 */
#define THR_DOUBLECLICK 16

/*
 * eBeam kernel driver use integer (long long) math.
 * We scale computed H matrix by a 10^PRECISION factor before
 * converting to long long values.
 * NOTE :
 * long long can held 2^63,
 * internal calculus (in long long) involves int*coef,
 * so, worst case, coef have to be less than 2^48, approx 10^14.
 *
 * Below 10^9, calulated screen value may be inaccurate.
 * Above 10^14, calculus may overflow.
 *
 * Defaulting to 10^12
 */
#define PRECISION 12

/// Names of the points
enum {
    UL = 0,  // Upper-left
    LL = 1,  // Lower-left
    UR = 2,  // Upper-right
    LR = 3,  // Lower-right
    NUM_POINTS
};

/// struct to hold associated device and screen coordinates
struct Tuple {
    int dev_X;
    int dev_Y;
    int scr_x;
    int scr_y;
};

/// struct to hold all tuples
struct Tuples {
    int num;
    Tuple tuple[NUM_POINTS];
};

/// Class for calculating new calibration parameters
class Calibrator
{
public:
    Calibrator(XID device_id0,
               const char* const device_name0,
               const char* const device_dir0,
               const int precision0,
               const int threshold_doubleclick0,
               const int z_min_x0,
               const int z_min_y0,
               const int z_max_x0,
               const int z_max_y0,
               const char* ifile0,
               const char* ofile0);

    ~Calibrator();

    // Parse arguments and create calibrator for gui
    static Calibrator* make_calibrator_gui(int argc, char** argv);

    // Parse arguments and create calibrator for cli
    static Calibrator* make_calibrator_cli(int argc, char** argv);

    // Find a eBeam device (using XInput) and fill device_id and device_name
    // Returns the number of devices found,
    // If pre_device is NULL, the last eBeam device found is selected.
    static int find_device(const char* pre_device,
                           bool list_devices,
                           XID& device_id,
                           const char*& device_name,
			   const char*& device_dir);

    // get the device Id
    XID get_device_id() { return device_id; };

    // get the device name
    const char* get_device_name() { return device_name; };

    // get the active zone coordinates
    bool is_zoned() { return zoned; };
    int get_min_x() { return min_x; };
    int get_min_y() { return min_y; };
    int get_max_x() { return max_x; };
    int get_max_y() { return max_y; };

    // get the number of clicks already registered
    int get_numclicks() const { return tuples.num; }

    // reset valid clicks count
    void reset_tuples() { tuples.num = 0; }

    // add a click with the given coordinates
    bool add_click(int X, int Y, int x, int y);

    // gui part done, finish calibration
    bool finish();

    // reset ebeam device
    bool reset_ebeam_calibration();

    // get calibration data from ebeam driver
    bool get_ebeam_calibration();

    // set ebeam calibration data from calibrator's data
    bool set_ebeam_calibration();

    // reset evdev to uncalibrated
    bool reset_evdev_calibration();

    // sync evdev calibration
    bool sync_evdev_calibration();

    // save/restore calibration data
    bool do_calib_io();

    // Be verbose or not
    static bool verbose;

private:
    // compute X11 Coordinate Transformation Matrix
    void compute_XCTM(float* m);
    
    // reset XCTM to identity
    void set_XCTM_to_identity(float* m);
    
    // Compute homograpĥy matrix from tuples
    bool find_H();

    // test H
    bool test_H();

private:
    // X objects
    Display     *display;
    XDeviceInfo *devInfo;
    XDevice     *dev;

    // XID of the device
    const XID device_id;

    // Name of the device
    const char* const device_name;

    // sysfs path to the device
    const char* const device_dir;

    // Precision : H matrix coefs are scaled by 10^precision
    // before long long conversion.
    int precision;

    // Threshold to keep the same point from being clicked twice.
    int threshold_doubleclick;

    // associated  coordinates
    Tuples tuples;

    // calibration data
    // H matrix
    long long H[9];

    // zone geometry
    bool zoned;
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    // screen geometry
    int screen_width;
    int screen_height;

    // file path to save/restore
    const char* const ifile;
    const char* const ofile;
};

#endif
