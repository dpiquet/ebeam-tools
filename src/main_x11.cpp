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
#include "gui/x11.hpp"

#include <unistd.h>

int main(int argc, char** argv)
{
    Calibrator* calibrator = Calibrator::make_calibrator_gui(argc, argv);

    GuiCalibratorX11::make_instance( calibrator );

    // processes events
    while(GuiCalibratorX11::is_running)
        pause();
    
    GuiCalibratorX11::destroy_instance();
    delete calibrator;
    
    return 0;
}
