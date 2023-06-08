/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPEG-HTTP streamer.                   #
#                                                                            #
#    Copyright (C) 2018-2023  Maxim Devaev <mdevaev@gmail.com>               #
#                                                                            #
#    This program is free software: you can redistribute it and/or modify    #
#    it under the terms of the GNU General Public License as published by    #
#    the Free Software Foundation, either version 3 of the License, or       #
#    (at your option) any later version.                                     #
#                                                                            #
#    This program is distributed in the hope that it will be useful,         #
#    but WITHOUT ANY WARRANTY; without even the implied warranty of          #
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           #
#    GNU General Public License for more details.                            #
#                                                                            #
#    You should have received a copy of the GNU General Public License       #
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.  #
#                                                                            #
*****************************************************************************/


#pragma once

#define US_VERSION_MAJOR 5
#define US_VERSION_MINOR 40

#define US_MAKE_VERSION2(_major, _minor) #_major "." #_minor
#define US_MAKE_VERSION1(_major, _minor) US_MAKE_VERSION2(_major, _minor)
#define US_VERSION US_MAKE_VERSION1(US_VERSION_MAJOR, US_VERSION_MINOR)

#define US_VERSION_U ((unsigned)(US_VERSION_MAJOR * 1000 + US_VERSION_MINOR))
