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

#include "calibrator.hpp"

#include <sys/types.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <math.h>

#include <stdexcept>
#include <iostream>
#include <fstream>

// X
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XI2proto.h>

#ifdef HAVE_X11_XRANDR
#include <X11/extensions/Xrandr.h> // support for multi-head setups
#endif

// gnu gsl
#include <gsl/gsl_errno.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_permutation.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_blas.h>

/// static verbose
bool Calibrator::verbose = false;

/// strdup helper : non-ansi
static char* my_strdup(const char* s) {
    size_t len = strlen(s) + 1;
    void* p = malloc(len);

    if (p == NULL)
        return NULL;

    return (char*) memcpy(p, s, len);
}

Calibrator::Calibrator(XID device_id0,
                       const char* const device_name0,
                       const char* const device_dir0,
                       const int precision0,
                       const int threshold_doubleclick0,
                       const int z_min_x0,
                       const int z_min_y0,
                       const int z_max_x0,
                       const int z_max_y0,
                       const char* ifile0,
                       const char* ofile0)
  : device_id(device_id0),
    device_name(device_name0),
    device_dir(device_dir0),
    precision(precision0),
    threshold_doubleclick(threshold_doubleclick0),
    min_x(z_min_x0),
    min_y(z_min_y0),
    max_x(z_max_x0),
    max_y(z_max_y0),
    ifile(ifile0),
    ofile(ofile0)
{
    int screen_num;
    
    reset_tuples();

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        throw std::runtime_error("Unable to connect to X server.");
    }

    screen_num = DefaultScreen(display);

#ifdef HAVE_X11_XRANDR
    int nsizes;

    XRRScreenSize* randrsize = XRRSizes(display, screen_num, &nsizes);
    if (nsizes != 0) {
        Rotation screenrot = 0;
        XRRRotations(display, screen_num, &screenrot);
        bool rot = screenrot & RR_Rotate_90 || screenrot & RR_Rotate_270;
        screen_width = rot ? randrsize->height : randrsize->width;
        screen_height = rot ? randrsize->width : randrsize->height;
    } else {
        screen_width = DisplayWidth(display, screen_num);
        screen_height = DisplayHeight(display, screen_num);
    }
#else
    screen_width = DisplayWidth(display, screen_num);
    screen_height = DisplayHeight(display, screen_num);
#endif

    zoned = true;
    if (!(min_x | min_y | max_x | max_y)) {
        max_x = screen_width -1;
        max_y = screen_height -1;
	zoned = false;
    }
    
    dev = XOpenDevice(display, device_id);
    if (!dev) {
        XCloseDisplay(display);
        throw std::runtime_error("Unable to open device.");
    }
}

Calibrator::~Calibrator ()
{
    XCloseDevice(display, dev);
    XCloseDisplay(display);
}

///
/// static members
///

static void usage_gui(char* cmd)
{
    fprintf(stderr, "Usage: %s [options]\n", cmd);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "\t-h, --help: print this help message\n");
    fprintf(stderr, "\t-v, --verbose: "
                    "print debug messages during the process\n");
    fprintf(stderr, "\t--list: list calibratable input devices and quit\n");
    fprintf(stderr, "\t--device <device name or id>: "
                    "select a specific device to calibrate\n");
    fprintf(stderr, "\t--zone <min_x min_y max_x max_y>: set the active zone "
                    "(default: full screen)\n");
    fprintf(stderr, "\t--precision: set the number of digit precision "
                    "(default: %i)\n", PRECISION);
    fprintf(stderr, "\t--threshold: set the misclick threshold "
                    "(0=off, default: %i)\n", THR_DOUBLECLICK);
}

Calibrator* Calibrator::make_calibrator_gui(int argc, char** argv)
{
    bool list_devices = false;
    const char* pre_device = NULL;
    int thr_doubleclick = THR_DOUBLECLICK;
    int precision = PRECISION;
    int z_min_x = 0;
    int z_min_y = 0;
    int z_max_x = 0;
    int z_max_y = 0;

    // parse input
    if (argc > 1) {
        for (int i=1; i!=argc; i++) {
            // Display help ?
            if (strcmp("-h", argv[i]) == 0 ||
                strcmp("--help", argv[i]) == 0) {
                fprintf(stderr, "ebeam_calibrator v%s\n\n", VERSION);
                usage_gui(argv[0]);
                exit(0);
            } else

            // Verbose output ?
            if (strcmp("-v", argv[i]) == 0 ||
                strcmp("--verbose", argv[i]) == 0) {
                verbose = true;
                fprintf(stderr, "ebeam_calibrator v%s\n", VERSION);
            } else

            // Just list devices ?
            if (strcmp("--list", argv[i]) == 0) {
                list_devices = true;
            } else

            // Select specific device ?
            if (strcmp("--device", argv[i]) == 0) {
                if (argc > i+1)
                    pre_device = argv[++i];
                else {
                    fprintf(stderr, "Error: --device needs a device name or id "
                                    "as argument;\n");
                    fprintf(stderr, "       Use --list to list the "
                                    "calibratable input devices.\n\n");
                    usage_gui(argv[0]);
                    exit(1);
                }
            } else

            // Get precision ?
            if (strcmp("--precision", argv[i]) == 0) {
                if (argc > i+1)
                    precision = atoi(argv[++i]);
                else {
                    fprintf(stderr, "Error: --precision needs a number "
                                    "as argument.\n");
                    usage_gui(argv[0]);
                    exit(1);
                }
            } else

            // Get zone ?
            if (strcmp("--zone", argv[i]) == 0) {
                if (argc > i+4) {
                    z_min_x = atoi(argv[++i]);
                    z_min_y = atoi(argv[++i]);
                    z_max_x = atoi(argv[++i]);
                    z_max_y = atoi(argv[++i]);
		}
                else {
                    fprintf(stderr, "Error: --zone needs 4 numbers "
                                    "as argument.\n");
                    usage_gui(argv[0]);
                    exit(1);
                }
            } else

              // Get mis-click threshold ?
            if (strcmp("--threshold", argv[i]) == 0) {
                if (argc > i+1)
                    thr_doubleclick = atoi(argv[++i]);
                else {
                    fprintf(stderr, "Error: --threshold needs a number "
                                    "as argument.\n");
                    fprintf(stderr, "       Set to 0 to disable "
                                    "mis-click detection.\n\n");
                    usage_gui(argv[0]);
                    exit(1);
                }
            } else {

                // unknown option
                fprintf(stderr, "Error: Unknown option: %s\n\n", argv[i]);
                usage_gui(argv[0]);
                exit(1);
            }
        }
    }

    // Find the device
    XID         device_id   = (XID) -1;
    const char* device_name = NULL;
    const char* device_dir  = NULL;

    int nr_found = find_device(pre_device, list_devices,
                               device_id, device_name, device_dir);

    if (list_devices) {
        // list printed in find_device
        if (nr_found == 0)
            printf("No eBeam device found.\n");
        exit(1);
    }

    if (nr_found == 0) {
        if (pre_device == NULL)
            fprintf(stderr, "Error: No eBeam device found.\n");
        else {
            fprintf(stderr, "Error: Device \"%s\" not found;\n", pre_device);
            fprintf(stderr, "       Use --list to list the input devices.\n");
        }
        exit(1);

    } else if (nr_found > 1) {
        fprintf(stderr, "Warning: multiple eBeam devices found.\n");
        fprintf(stderr, "         Calibrating last one ('%s')\n", device_name);
        fprintf(stderr, "         Use --device to select another one.\n");
    }

    if (verbose) {
        fprintf(stderr, "Selected device: '%s'\n", device_name);
    }

    return new Calibrator(device_id, device_name, device_dir,
                          precision, thr_doubleclick,
			  z_min_x, z_min_y, z_max_x, z_max_y,
			  NULL, NULL);
}

static void usage_cli(char* cmd)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "\t%s -h, --help: print this help message\n", cmd);
    fprintf(stderr, "\t%s [options] --list: "
                    "list calibratable input devices and quit.\n", cmd);
    fprintf(stderr, "\t%s [options] --save <file>: "
                    "save current calibration to file.\n", cmd);
    fprintf(stderr, "\t%s [options] --restore <file>: "
                    "restore calibration from file.\n", cmd);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "\t-v, --verbose: "
                    "print debug messages during the process.\n");
    fprintf(stderr, "\t--device <device name or id>: "
                    "select a specific device.\n");
}

Calibrator* Calibrator::make_calibrator_cli(int argc, char** argv)
{
    bool list_devices = false;
    const char* pre_device = NULL;
    const char* ifile = NULL;
    const char* ofile = NULL;

    // parse input
    if (argc > 1) {
        for (int i=1; i!=argc; i++) {
            // Display help ?
            if (strcmp("-h", argv[i]) == 0 ||
                strcmp("--help", argv[i]) == 0) {
                fprintf(stderr, "ebeam_state v%s\n\n", VERSION);
                usage_cli(argv[0]);
                exit(0);
            } else

            // Verbose output ?
            if (strcmp("-v", argv[i]) == 0 ||
                strcmp("--verbose", argv[i]) == 0) {
                verbose = true;
                fprintf(stderr, "ebeam_state v%s\n", VERSION);
            } else

            // Just list devices ?
            if (strcmp("--list", argv[i]) == 0) {
                list_devices = true;
            } else

            // Select specific device ?
            if (strcmp("--device", argv[i]) == 0) {
                if (argc > i+1)
                    pre_device = argv[++i];
                else {
                    fprintf(stderr, "Error: --device needs a device name or "
                                    "id as argument;\n");
                    fprintf(stderr, "       Use --list to list the calibratable"
                                    "input devices.\n\n");
                    usage_cli(argv[0]);
                    exit(1);
                }
            } else

            // Save ?
            if (strcmp("--save", argv[i]) == 0) {
                if (argc > i+1)
                    ofile = argv[++i];
                else {
                    fprintf(stderr, "Error: --save needs a file name "
                                    "as argument;\n");
                    usage_cli(argv[0]);
                    exit(1);
                }

            } else

            // Restore ?
            if (strcmp("--restore", argv[i]) == 0) {
                if (argc > i+1)
                    ifile = argv[++i];
                else {
                    fprintf(stderr, "Error: --restore needs a file name "
                                    "as argument;\n");
                    usage_cli(argv[0]);
                    exit(1);
                }

            } else {

                // unknown option
                fprintf(stderr, "Error: Unknown option: %s\n\n", argv[i]);
                usage_cli(argv[0]);
                exit(1);
            }
        }
    } else {

        // no arg
        fprintf(stderr, "Error: missing command.\n\n");
        usage_cli(argv[0]);
        exit(1);
    }

    // Find the device
    XID         device_id   = (XID) -1;
    const char* device_name = NULL;
    const char* device_dir  = NULL;

    int nr_found = find_device(pre_device, list_devices,
                               device_id, device_name, device_dir);

    if (list_devices) {
        // list printed in find_device
        if (nr_found == 0) {
            printf("No eBeam device found.\n");
            exit(1);
        }
        exit(0);
    }

    if (nr_found == 0) {
        if (pre_device == NULL)
            fprintf(stderr, "Error: No eBeam device found.\n");
        else {
            fprintf(stderr, "Error: Device '%s' not found;\n", pre_device);
            fprintf(stderr, "       Use --list to list the input devices.\n");
        }
        exit(1);

    } else if (nr_found > 1) {
        fprintf(stderr, "Warning: multiple eBeam devices found.\n");
        fprintf(stderr, "         Calibrating last one ('%s')\n", device_name);
        fprintf(stderr, "         Use --device to select another one.\n");
    }

    if (verbose) {
        fprintf(stderr, "Selected device: '%s'\n", device_name);
    }

    return new Calibrator(device_id, device_name, device_dir,
                          PRECISION, THR_DOUBLECLICK,
                          0, 0, 0, 0,
                          ifile, ofile);
}

int Calibrator::find_device(const char* pre_device,
                            bool list_devices,
                            XID& device_id,
                            const char*& device_name,
			    const char*& device_dir)
{
    bool pre_device_is_id = true;
    int found = 0;
    int xi_opcode;
    int event;
    int error;
    int ndevices;        // number of input devices found
    char *device_event;
    char buffer[128];
    
    Display* display;
    Atom prop;
    Atom act_type;
    int act_format;
    unsigned long nitems, bytes_after;
    unsigned char *data;
    XDeviceInfoPtr list;
    XDeviceInfoPtr slist;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "ERROR: Unable to connect to X server.\n");
        return 0;
    }

    if (!XQueryExtension(display, "XInputExtension",
                                  &xi_opcode, &event, &error)) {
        fprintf(stderr, "ERROR : X Input extension not available.\n");
        return 0;
    }

    // verbose, get Xi version
    if (verbose) {
        XExtensionVersion *version = XGetExtensionVersion(display, INAME);

        if (version && (version != (XExtensionVersion*) NoSuchExtension)) {
            fprintf(stderr, "%s version is %i.%i\n",
                   INAME, version->major_version, version->minor_version);
            XFree(version);
        }
    }

    // device's node property : /dev/input/eventXX
    prop = XInternAtom (display, "Device Node", False);
    if (!prop) {
        fprintf(stderr, "ERROR : Device Node property not found\n");
	return 0;
    }
    
    if (pre_device != NULL) {
        // check whether the pre_device is an ID (only digits)
        int len = strlen(pre_device);
        for (int loop=0; loop<len; loop++) {
            if (!isdigit(pre_device[loop])) {
                pre_device_is_id = false;
                break;
            }
        }
    }

    // get input devices list
    slist=list=(XDeviceInfoPtr) XListInputDevices(display, &ndevices);

    for (int i=0; i<ndevices; i++, list++)
    {
        if (list->use == IsXKeyboard || list->use == IsXPointer)
            // virtual master device
            continue;

        // if we are looking for a specific device
        if (pre_device != NULL) {
            if ((pre_device_is_id && list->id == (XID) atoi(pre_device)) ||
                (!pre_device_is_id && strcmp(list->name, pre_device) == 0)) {
                // OK, fall through
            } else {
                // skip, not this device
                continue;
            }
        }

        if (!strstr(list->name,"eBeam")) // name must contains "eBeam"
            continue;

        XAnyClassPtr any = (XAnyClassPtr) (list->inputclassinfo);
        for (int j=0; j<list->num_classes; j++)
        {
            if (any->c_class == ValuatorClass)
            {
                XValuatorInfoPtr V = (XValuatorInfoPtr) any;
                XAxisInfoPtr ax = (XAxisInfoPtr) V->axes;

                if (V->mode != Absolute) {
                    if (verbose)
                        fprintf(stderr, "Skipping device '%s' id=%i : "
                                        "does not report Absolute events.\n",
                                        list->name, (int)list->id);
                } else if (V->num_axes < 2 ||
                           (ax[0].min_value == -1 && ax[0].max_value == -1) ||
                           (ax[1].min_value == -1 && ax[1].max_value == -1)) {
                    if (verbose)
                        fprintf(stderr, "Skipping device '%s' id=%i : does "
                                        "not have two calibratable axes.\n",
                                        list->name, (int)list->id);
                } else {
                    /* eBeam device found */

                    // check Device Node
                    if (!XIGetProperty (display, list->id, prop, 0, 1000, False,
                                        AnyPropertyType, &act_type, &act_format,
                                        &nitems, &bytes_after, &data) == Success) {
                        if (verbose)
                            fprintf(stderr, "Skipping device '%s' id=%i : "
                                            "no device node.\n",
                                            list->name, (int)list->id);
                        continue;
                    }

                    if (nitems == 0) {
                        if (verbose)
                            fprintf(stderr, "Skipping device '%s' id=%i : "
                                            "0 device node.\n",
                                            list->name, (int)list->id);
			continue;
                    }
                    
                    if ( !( (act_type == XA_STRING) && (act_format == 8) ) ) {
                        if (verbose)
                            fprintf(stderr, "Skipping device '%s' id=%i : "
                                            "bad device node format.\n",
                                            list->name, (int)list->id);
                        XFree(data);
			continue;
                    }
                    
                    // All clear, good device
		    found++;
                    device_id = list->id;
                    device_name = my_strdup(list->name);
                    device_event = my_strdup(strstr((char *) data, "event"));
		    sprintf(buffer,
			    "/sys/class/input/%s/device/device/", device_event);
                    device_dir = my_strdup(buffer);
                    XFree (data);
    
                    if (list_devices)
                        printf("Device '%s' id=%i (%s)\n",
                               device_name, (int)device_id, device_event);
                    if (verbose)
                        fprintf(stderr, "  Using %s sysfs directory.\n",
                                        device_dir);
                }
            }

            /*
             * Increment 'any' to point to the next item in the linked
             * list.  The length is in bytes, so 'any' must be cast to
             * a character pointer before being incremented.
             */
            any = (XAnyClassPtr) ((char *) any + any->length);
        }
    }

    XFreeDeviceList(slist);
    XCloseDisplay(display);

    return found;
}

///
/// regular members
///

bool Calibrator::add_click(int X, int Y, int x, int y)
{
    int num = tuples.num; // current tuple added

    // Double-click detection
    if (threshold_doubleclick > 0 && num > 0) {
        int i = num - 1;
        while (i >= 0) {
            if (   abs(X - tuples.tuple[i].dev_X) <= threshold_doubleclick
                && abs(Y - tuples.tuple[i].dev_Y) <= threshold_doubleclick) {
                if (verbose)
                    fprintf(stderr, "Not adding click %i raw(%i, %i) : "
                                    "within %i units of previous click\n",
                                    num+1, X, Y, threshold_doubleclick);
                return FAILURE;
            }
            i--;
        }
    }

    tuples.tuple[num].dev_X = X;
    tuples.tuple[num].dev_Y = Y;
    tuples.tuple[num].scr_x = x;
    tuples.tuple[num].scr_y = y;
    tuples.num++;

    if (verbose)
        fprintf(stderr, "Adding click %i : raw(%i, %i) <=> screen(%i, %i)\n",
                        tuples.num, X, Y, x, y);

    return SUCCESS;
}

bool Calibrator::finish()
{
    if (tuples.num != NUM_POINTS){
        fprintf(stderr, "ERROR: not enough points.\n");
        return FAILURE;
    }

    if (!find_H()) {
        fprintf(stderr, "ERROR: unable to compute H matrix.\n");
        return FAILURE;
    }

    if (!test_H()) {
        fprintf(stderr, "ERROR: unreliable H matrix.\n");
        return FAILURE;
    }

    if (!set_ebeam_calibration()) {
        fprintf(stderr, "ERROR: unable to set eBeam calibration.\n");
        return FAILURE;
    }

    if (!sync_evdev_calibration()) {
        fprintf(stderr, "ERROR: unable to set X calibration.\n");
        return FAILURE;
    }

    return SUCCESS;
}

bool Calibrator::reset_ebeam_calibration()
{
    char fname[100];
    sprintf(fname, "%s", device_dir);
    strcat(fname, "calibrated");

    FILE *fp;
    if ( !(fp = fopen(fname, "w")) ) {
        fprintf(stderr, "ERROR: unable to open %s for writing.\n", fname);
        return FAILURE;
    }
    fprintf(fp, "0");
    fclose(fp);

    if (verbose)
        fprintf(stderr, "eBeam calibration resetted.\n");

    return SUCCESS;
}

bool Calibrator::get_ebeam_calibration()
{
    FILE *fp;
    char fname[100];

#define READ(DATA)                                                             \
    sprintf(fname, "%s%s", device_dir, #DATA);                                 \
    if ( !(fp = fopen(fname, "r")) ) {                                         \
        fprintf(stderr, "ERROR: unable to open %s\n", fname);                  \
        return FAILURE;                                                        \
    }                                                                          \
    if ( !(fscanf(fp, "%d", &DATA)) ) {                                        \
        fprintf(stderr, "ERROR: unable to parse %s\n", fname);                 \
        fclose(fp);                                                            \
        return FAILURE;                                                        \
    }                                                                          \
    if (verbose)                                                               \
        fprintf(stderr, "Read %d from %s\n", DATA, fname);                     \
    fclose(fp);

READ(min_x)
READ(min_y)
READ(max_x)
READ(max_y)


    for (int i=1; i<=9; i++) {
        sprintf(fname, "%sh%d", device_dir, i);
        fp = fopen(fname, "r");
        if ( !(fp = fopen(fname, "r")) ) {
            fprintf(stderr, "ERROR: unable to open %s for reading.\n", fname);
            return FAILURE;
        }
        if ( !(fscanf(fp, "%lld", &H[i-1])) ) {
            fprintf(stderr, "ERROR: unable to parse %s\n", fname);
            fclose(fp);
            return FAILURE;
        }
        if (verbose)
            fprintf(stderr, "Read %lld from %s\n", H[i-1], fname);
        fclose(fp);
    }

    return SUCCESS;
}

bool Calibrator::set_ebeam_calibration()
{
    int n = 0; // number of file written
    DIR* dp = opendir(device_dir);

    if (dp == NULL) {
        fprintf(stderr, "ERROR: unable to open %s\n", device_dir);
        return FAILURE;
    }

    dirent* ep;
    while ((ep = readdir(dp))) {
        char fname[100];
        char value[50];
        sprintf(fname, "%s", device_dir); // end with /

        if (strcmp(ep->d_name, "min_x") == 0) {
            strcat(fname, "min_x");
            sprintf(value, "%d", min_x);
        } else

        if (strcmp(ep->d_name, "min_y") == 0) {
            strcat(fname, "min_y");
            sprintf(value, "%d", min_y);
        } else

        if (strcmp(ep->d_name, "max_x") == 0) {
            strcat(fname, "max_x");
            sprintf(value, "%d", max_x);
        } else

        if (strcmp(ep->d_name, "max_y") == 0) {
            strcat(fname, "max_y");
            sprintf(value, "%d", max_y);
        } else

        if (strcmp(ep->d_name, "h1") == 0) {
            strcat(fname, "h1");
            sprintf(value, "%lld", H[0]);
        } else

        if (strcmp(ep->d_name, "h2") == 0) {
            strcat(fname, "h2");
            sprintf(value, "%lld", H[1]);
        } else

        if (strcmp(ep->d_name, "h3") == 0) {
            strcat(fname, "h3");
            sprintf(value, "%lld", H[2]);
        } else

        if (strcmp(ep->d_name, "h4") == 0) {
            strcat(fname, "h4");
            sprintf(value, "%lld", H[3]);
        } else

        if (strcmp(ep->d_name, "h5") == 0) {
            strcat(fname, "h5");
            sprintf(value, "%lld", H[4]);
        } else

        if (strcmp(ep->d_name, "h6") == 0) {
            strcat(fname, "h6");
            sprintf(value, "%lld", H[5]);
        } else

        if (strcmp(ep->d_name, "h7") == 0) {
            strcat(fname, "h7");
            sprintf(value, "%lld", H[6]);
        } else

        if (strcmp(ep->d_name, "h8") == 0) {
            strcat(fname, "h8");
            sprintf(value, "%lld", H[7]);
        } else

        if (strcmp(ep->d_name, "h9") == 0) {
            strcat(fname, "h9");
            sprintf(value, "%lld", H[8]);
        } else
            continue;

        if (verbose)
            fprintf(stderr, "Writing %s to %s\n", value, fname);

        // do the write
        FILE *fp;

        if ( !(fp = fopen(fname, "w")) ) {
            fprintf(stderr, "ERROR: unable to open %s for writing.\n", fname);
            closedir(dp);
            return FAILURE;
        }

        fprintf(fp, "%s", value);
        fclose(fp);
        n++;
    }

    if (n != 13) {
        fprintf(stderr, "ERROR: only %d parameters set, "
                        "not in sync with ebeam kernel module ?\n", n);
        closedir(dp);
        return FAILURE;
    }

    // enabling calibration
    char fname[100];
    sprintf(fname, "%s", device_dir);
    strcat(fname, "calibrated");

    FILE *fp;

    if ( !(fp = fopen(fname, "w")) ) {
        fprintf(stderr, "ERROR: unable to open %s for writing.\n", fname);
        closedir(dp);
        return FAILURE;
    }

    fprintf(fp, "%u", 1);
    fclose(fp);

    if (verbose)
        fprintf(stderr, "eBeam calibration done\n");

    closedir(dp);

    return SUCCESS;
}

bool Calibrator::sync_evdev_calibration()
{
    // "Evdev Axis Calibration"
    // 4 32-bit values, order min-x, max-x, min-y, max-y

    /*
    * XChangeDeviceProperty(display, dev, prop, XA_INTEGER, 32,
    *                                PropModeReplace, data.c, 4);
    * use long *l (data.l)
    *
    * XIChangeProperty(display, device_id, prop, XA_INTEGER, 32,
    *                           PropModeReplace, data.c, 4);
    * use uint32_t *l (data.l)
    */

    Atom prop, prop_float;

    union {
        unsigned char *c;
        uint32_t *l;
    } data_i;

    union {
        unsigned char *c;
        float *f;
    } data_f;
    
    // Axis Calibration
    prop = XInternAtom(display, "Evdev Axis Calibration", False);
    if (prop == None) {
        fprintf(stderr, "ERROR : Evdev Axis Calibration property not found.\n");
        return FAILURE;
    }

    data_i.c = (unsigned char*)calloc(4, sizeof(uint32_t));

    data_i.l[0] = min_x;
    data_i.l[1] = max_x;
    data_i.l[2] = min_y;
    data_i.l[3] = max_y;

    XIChangeProperty(display, device_id, prop, XA_INTEGER, 32, PropModeReplace,
                     data_i.c, 4);

    XSync(display, false);
    free(data_i.c);

    // Set Coordinate Transformation Matrix if not fullscreen zone
    if (zoned) {
        prop_float = XInternAtom(display, "FLOAT", False);
        if (prop_float == None) {
            fprintf(stderr, "ERROR : Float atom not found..\n");
            return FAILURE;
        }
        prop = XInternAtom(display, "Coordinate Transformation Matrix", False);
        if (prop == None) {
            fprintf(stderr, "ERROR : Coordinate Transformation Matrix property not found.\n");
            return FAILURE;
        }

        data_f.c = (unsigned char*)calloc(9, sizeof(float));

        compute_XCTM(data_f.f);

        XIChangeProperty(display, device_id, prop, prop_float, 32, PropModeReplace,
                         data_f.c, 9);

        XSync(display, false);
        free(data_f.c);
    }
    
    if (verbose)
        fprintf(stderr, "Evdev calibration sync done.\n");

    return SUCCESS;
}

bool Calibrator::reset_evdev_calibration()
{
    // "Evdev Axis Calibration"
    // a number of 0 value reset evdev to uncalibrated.

    /*
    * XChangeDeviceProperty(display, dev, prop, XA_INTEGER, 32,
    *                                PropModeReplace, data.c, 4);
    * use long *l (data.l)
    *
    * XIChangeProperty(display, device_id, prop, XA_INTEGER, 32,
    *                           PropModeReplace, data.c, 4);
    * use uint32_t *l (data.l)
    */

    Atom prop, prop_float;

    union {
        unsigned char *c;
        uint32_t *l;
    } data;

    union {
        unsigned char *c;
        float *f;
    } data_f;
    
    prop = XInternAtom(display, "Evdev Axis Calibration", False);
    if (prop == None) {
        fprintf(stderr, "ERROR : Evdev Axis Calibration property not found.\n");
        return FAILURE;
    }

    XIChangeProperty(display, device_id, prop, XA_INTEGER, 32, PropModeReplace,
                     data.c, 0);

    XSync(display, false);

    // Set Coordinate Transformation Matrix to identity
    prop_float = XInternAtom(display, "FLOAT", False);
    if (prop_float == None) {
        fprintf(stderr, "ERROR : Float atom not found..\n");
        return FAILURE;
    }

    prop = XInternAtom(display, "Coordinate Transformation Matrix", False);
    if (prop == None) {
        fprintf(stderr, "ERROR : Coordinate Transformation Matrix property not found.\n");
        return FAILURE;
    }

    data_f.c = (unsigned char*)calloc(9, sizeof(float));

    set_XCTM_to_identity(data_f.f);

    XIChangeProperty(display, device_id, prop, prop_float, 32, PropModeReplace,
                     data_f.c, 9);

    XSync(display, false);
    free(data_f.c);

    if (verbose)
        fprintf(stderr, "Evdev calibration reset done.\n");

    return SUCCESS;
}

bool Calibrator::do_calib_io()
{
    FILE *fp;

    if (ifile && ofile && verbose)
        fprintf(stderr, "WARNING: Doing save and restore.\n");

    if (!ifile && !ofile) {
        fprintf(stderr, "ERROR: No file to save/restore.\n");
        return FAILURE;
    }

    // saving
    if (ofile) {
        if ( !(fp = fopen(ofile, "w")) ) {
            fprintf(stderr, "ERROR: unable to open %s for writing.\n", ofile);
            return FAILURE;
        }

        // get current calibration data
        if ( !get_ebeam_calibration() ) {
            fprintf(stderr, "ERROR: unable to retrieve actual calibration.\n");
            fclose(fp);
            return FAILURE;
        }

        // version
        fprintf(fp, "%s\n", VERSION);

        // min/max
        fprintf(fp, "%d\n%d\n%d\n%d\n", min_x, max_x, min_y, max_y);

        // H matrix
        for (int i = 0; i<9 ; i++)
            fprintf(fp, "%lld\n",H[i]);

        fclose(fp);

        if (verbose)
            fprintf(stderr, "Calibration data saved to %s\n", ofile);
    }

    // restoring
    if (ifile) {
        char version[10];

        if ( !(fp = fopen(ifile, "r")) ) {
            fprintf(stderr, "ERROR: unable to open %s for reading.\n", ifile);
            return FAILURE;
        }

        // version check
        // for now, keep going
        if (fscanf(fp, "%s\n", version) != 1) {
            fprintf(stderr, "ERROR: bad state file (version) %s\n", ifile);
            fclose(fp);
            return FAILURE;
        }
        
        if (strcmp(version, VERSION) != 0) {
            fprintf(stderr, "WARNING: version mismatch : state file is %s, "
                            "application is %s.\n", version, VERSION);
            fprintf(stderr, "         Proceeding anyway.\n");
        }

        // min/max
        if (fscanf(fp, "%d\n%d\n%d\n%d\n",
                       &min_x, &max_x, &min_y, &max_y) != 4) {
            fprintf(stderr, "ERROR: bad state file (min/max) %s\n", ifile);
            fclose(fp);
            return FAILURE;
        }

        // H matrix
        for (int i = 0; i<9 ; i++)
            if ( !fscanf(fp, "%lld\n",&H[i]) ) {
                fprintf(stderr, "ERROR: bad state file (H coefs) %s\n", ifile);
                fclose(fp);
                return FAILURE;
            }

        fclose(fp);

        if ((min_x == 0) & (min_y == 0) & (max_x == screen_width -1) & (max_y == screen_height -1)) {
            zoned = false;
            if (verbose)
                fprintf(stderr, "Active zone : full screen\n");
        } else {
            zoned = true;
            if (verbose)
                fprintf(stderr, "Active zone : %i %i %i %i\n", min_x, min_y, max_x, max_y);
        }

        if (!set_ebeam_calibration()) {
            fprintf(stderr, "ERROR: unable to set eBeam calibration.\n");
            return FAILURE;
        }

        if (!sync_evdev_calibration()) {
            fprintf(stderr, "ERROR: unable to set X calibration.\n");
            return FAILURE;
        }

        if (verbose)
            fprintf(stderr, "Calibration data restored from %s\n", ifile);
    }

    return SUCCESS;
}

void Calibrator::compute_XCTM(float* m)
{
    m[0] = (max_x - min_x +1)/((float)screen_width);
    m[1] = 0;
    m[2] = min_x/((float)screen_width);
    m[3] = 0;
    m[4] = (max_y - min_y +1)/((float)screen_height);
    m[5] = min_y/((float)screen_height);
    m[6] = 0;
    m[7] = 0;
    m[8] = 1;    

    if (verbose) {
        fprintf(stderr, "Computed X11 Coordinate Transformation Matrix :\n");
        for (int i=0; i<3; i++)
            fprintf(stderr, "[%19f ; %19f ; %19f]\n",
                            m[3*i], m[3*i+1], m[3*i+2]);
    }
}

void Calibrator::set_XCTM_to_identity(float* m)
{
    m[0] = 1;
    m[1] = 0;
    m[2] = 0;
    m[3] = 0;
    m[4] = 1;
    m[5] = 0;
    m[6] = 0;
    m[7] = 0;
    m[8] = 1;
}

bool Calibrator::find_H()
{
    /*
    * See :
    * http://www.csc.kth.se/~perrose/files/pose-init-model/node17_ct.html
    *
    * solve A.h=b instead of calculing h=inv(A).b
    */

    // disable gsl error handler
    gsl_set_error_handler_off();

    gsl_vector * h = gsl_vector_calloc(8); // H coefs (h11, h12, ... , h32)

    gsl_matrix * A = gsl_matrix_alloc (8, 8); // A : linear equations matrix

    // fill A, 2 row at a time
    for (int p=0; p<4; p++) {               // 4 tuples
        int X = tuples.tuple[p].dev_X;      // device
        int Y = tuples.tuple[p].dev_Y;
        int x = tuples.tuple[p].scr_x;      // screen
        int y = tuples.tuple[p].scr_y;

        gsl_matrix_set (A, p*2, 0, X);      // first row
        gsl_matrix_set (A, p*2, 1, Y);
        gsl_matrix_set (A, p*2, 2, 1);
        gsl_matrix_set (A, p*2, 3, 0);
        gsl_matrix_set (A, p*2, 4, 0);
        gsl_matrix_set (A, p*2, 5, 0);
        gsl_matrix_set (A, p*2, 6, -(X*x));
        gsl_matrix_set (A, p*2, 7, -(Y*x));

        gsl_matrix_set (A, p*2+1, 0, 0);    // second row
        gsl_matrix_set (A, p*2+1, 1, 0);
        gsl_matrix_set (A, p*2+1, 2, 0);
        gsl_matrix_set (A, p*2+1, 3, X);
        gsl_matrix_set (A, p*2+1, 4, Y);
        gsl_matrix_set (A, p*2+1, 5, 1);
        gsl_matrix_set (A, p*2+1, 6, -(X*y));
        gsl_matrix_set (A, p*2+1, 7, -(Y*y));
    }

    gsl_vector * b = gsl_vector_calloc(8);  // b coefs (x1, y1, .., x4, y4)

    for (int p=0; p<4; p++) {               // 4 tuples
        gsl_vector_set (b, p*2,   tuples.tuple[p].scr_x);
        gsl_vector_set (b, p*2+1, tuples.tuple[p].scr_y);
    }

    // LU decomposition
    int s;
    gsl_matrix * LU = gsl_matrix_calloc(8,8);
    gsl_permutation * p  = gsl_permutation_alloc(8);

    if (gsl_matrix_memcpy(LU,A)) {
        fprintf(stderr, "ERROR: gsl memcopy failed.\n");
        return FAILURE;
    }

    if (gsl_linalg_LU_decomp(LU,p,&s)) {
        fprintf(stderr, "ERROR: gsl LU decomposition failed.\n");
        return FAILURE;
    }

    // solve A * h = b
    if (gsl_linalg_LU_solve(LU, p, b, h)) {
        fprintf(stderr, "ERROR: gsl solver failed.\n");
        return FAILURE;
    }

    // fill H (long long) with rounded (h (double) scaled by 10^precision)
    for (int i=0; i<8; i++) {
        if (gsl_vector_get(h, i) >= 0)
            H[i] = (long long) (((long double) gsl_vector_get(h, i)) *
                                ((long double) pow(10.0,precision)) + 0.5);
        else
            H[i] = (long long) (((long double) gsl_vector_get(h, i)) *
                                ((long double) pow(10.0,precision)) - 0.5);
    }
    H[8] = (long long) pow(10.0,precision);

    gsl_permutation_free(p);
    gsl_matrix_free(LU);
    gsl_vector_free(h);
    gsl_matrix_free(A);
    gsl_vector_free(b);

    if (verbose) {
        fprintf(stderr, "Computed H matrix :\n");
        for (int i=0; i<3; i++)
            fprintf(stderr, "[%19lld ; %19lld ; %19lld]\n",
                            H[3*i], H[3*i+1], H[3*i+2]);
    }

    return SUCCESS;
}

bool Calibrator::test_H()
{
   /*
    * From ebeam.c kernel driver, keep in sync
    *
    * s64 scale;
    * scale = ebeam->cursetting.h7 * ebeam->X +
    *         ebeam->cursetting.h8 * ebeam->Y +
    *         ebeam->cursetting.h9;
    *
    * We *must* round the result, but not with (int) (v1/v2 + 0.5)
    *
    * (int) (v1/v2 + 0.5) <=> (int) ( (2*v1 + v2)/(2*v2) )
    *
    * ebeam->x = (int) ((((ebeam->cursetting.h1 * ebeam->X +
    *                      ebeam->cursetting.h2 * ebeam->Y +
    *                      ebeam->cursetting.h3) << 1) + scale) /
    *                      (scale << 1));
    * ebeam->y = (int) ((((ebeam->cursetting.h4 * ebeam->X +
    *                      ebeam->cursetting.h5 * ebeam->Y +
    *                      ebeam->cursetting.h6) << 1) + scale) /
    *                      (scale << 1));
    */

    for (int p=0; p<4; p++) { // points
        int X = tuples.tuple[p].dev_X; // device
        int Y = tuples.tuple[p].dev_Y;

        long long div = (H[6] * X + H[7] * Y + H[8]);
        if (div == 0) {
            if (verbose)
                fprintf(stderr, "ERROR: Bad H matrix : division by zero\n");
            return FAILURE;
        }

        int x = (int) ((2 * (H[0] * X + H[1] * Y + H[2]) + div)/(2*div));
        int y = (int) ((2 * (H[3] * X + H[4] * Y + H[5]) + div)/(2*div));

        if ((x != tuples.tuple[p].scr_x) || (y != tuples.tuple[p].scr_y)) {
            if (verbose)
                fprintf(stderr, "ERROR: Bad H matrix :\n");
                fprintf(stderr, "Point %i : dev(%i ; %i) => scr(%i ; %i), "
                                "real(%i ; %i)\n",
                                p+1, X, Y, x, y,
                                tuples.tuple[p].scr_x, tuples.tuple[p].scr_y);
            return FAILURE;
        }
    }

    return SUCCESS;
}















