/*****************************************************************************
#                                                                            #
#    uStreamer - Lightweight and fast MJPG-HTTP streamer.                    #
#                                                                            #
#    Copyright (C) 2018  Maxim Devaev <mdevaev@gmail.com>                    #
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

#include "../../config.h"


const char HTML_INDEX_PAGE[] = " \
	<!DOCTYPE html> \
	\
	<html> \
	<head> \
		<meta charset=\"utf-8\" /> \
		<title>uStreamer</title> \
	</head> \
	\
	<body> \
		<h3>&micro;Streamer v" VERSION "</h3> \
		<hr> \
		<ul> \
			<li> \
				<a href=\"/state\"><b><samp>/state</samp></b></a><br> \
				Get JSON structure with state of the server. \
			</li> \
			<br> \
			<li> \
				<a href=\"/snapshot\"><b><samp>/snapshot</samp></b></a><br> \
				Get a current actual image from the server. \
			</li> \
			<br> \
			<li> \
				<a href=\"/stream\"><b><samp>/stream</samp></b></a><br> \
				Get a live stream. Query params:<br> \
				<br> \
				<ul> \
					<li> \
						<b><samp>key=abc123</samp></b><br> \
						User-defined key, which is part of cookie <samp>stream_client</samp>, which allows<br> \
						the stream client to determine its identifier and view statistics using <a href=\"/state\"><samp>/state</samp></a>. \
					</li> \
					<br> \
					<li> \
						<b><samp>extra_headers=1</samp></b><br> \
						Add <samp>X-UStreamer-*</samp> headers to /stream handle (like on <a href=\"/snapshot\"><samp>/snapshot</samp></a>). \
					</li> \
					<br> \
					<li> \
						<b><samp>advance_headers=1</samp></b><br> \
						Enable workaround for Chromium/Blink \
						<a href=\"https://bugs.chromium.org/p/chromium/issues/detail?id=527446\">Bug #527446</a>. \
					</li> \
					<br> \
					<li> \
						<b><samp>dual_final_frames=1</samp></b><br> \
						Enable workaround for Safari/WebKit bug when using option <samp>--drop-same-frames</samp>.<br> \
						Without this option, when the frame series is completed, WebKit-based browsers<br> \
						renders the last frame with a delay. \
					</li> \
				</ul> \
			</li> \
			<br> \
		</ul> \
		<br> \
		<hr> \
		<a href=\"https://github.com/pikvm/ustreamer\">Sources &amp; docs</a> \
	</body> \
	</html> \
";
