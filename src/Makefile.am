SUBDIRS = gui

AM_CXXFLAGS = -Wall -ansi -pedantic

bin_PROGRAMS = ebeam_calibrator ebeam_state

COMMON_SRCS = calibrator.cpp

ebeam_calibrator_SOURCES = gui/x11.cpp main_x11.cpp $(COMMON_SRCS)
ebeam_calibrator_LDADD = $(XINPUT_LIBS) $(XRANDR_LIBS) $(X11_LIBS) $(GSL_LIBS)
ebeam_calibrator_CXXFLAGS = $(XINPUT_CFLAGS) $(X11_CFLAGS) $(XRANDR_CFLAGS) $(GSL_CFLAGS) $(AM_CXXFLAGS)

ebeam_state_SOURCES = main_cli.cpp $(COMMON_SRCS)
ebeam_state_LDADD = $(XINPUT_LIBS) $(XRANDR_LIBS) $(X11_LIBS) $(GSL_LIBS)
ebeam_state_CXXFLAGS = $(XINPUT_CFLAGS) $(X11_CFLAGS) $(XRANDR_CFLAGS) $(GSL_CFLAGS) $(AM_CXXFLAGS)

EXTRA_DIST = \
	calibrator.cpp \
	calibrator.hpp
