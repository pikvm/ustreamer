#!/bin/sh

set -e

[ -n "$EDID" ] && {
	[ -n "$EDID_HEX" ] && echo "$EDID_HEX" > /edid.hex
	while true; do
		v4l2-ctl --device=/dev/video0 --set-edid=file=/edid.hex --fix-edid-checksums --info-edid && break
		echo 'Failed to set EDID. Reetrying...'
		sleep 1
	done
}

./ustreamer --host=0.0.0.0 $@
