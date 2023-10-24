## Streaming to third party services

This method provides the ability of streaming a USB Webcam and include audio to large audiences.
It uses to two machines. One is a Raspberry Pi and the other a more capable machine to performance
the encoding of the video and audio that is streamed to the third party service such as YouTube.

Another benefit of using a browser (http stream) is the video can have overlays add in the custom ustreamer webpage.
For example a cron process that retrieves weather information and updates a file to include on the page, announcements,
or other creative ideas. The audio stream can also be something other than the webcam mic (music, voice files, etc.)
and easily changed on the second machine setup. In the following example filtering is applied in ffmpeg to
improve the sound of the webcam mic making vocals clearer and more intelligible.

* Machine 1:
    * USB webcam on the machine (Pi for example) running ustreamer (video) and VLC (audio). Remember to make any needed firewall changes if machine 2 is on a separate network so it can reach the ports for the video and audio.
    * To stream audio from the Pi.
        ```
        /usr/bin/vlc -I dummy -vvv alsa://hw:2,0 --sout #transcode{acodec=mp3,ab=128}:standard{access=http,mux=ts,dst=:[PickAPort}
        ```

* Machine 2:
    * On a more capable box run the video stream in a browser using ffmpeg to combine the video (browser) and audio and stream to YouTube or other services. In this example a VM with two virtual monitors running the browser full screen one of the monitors is used.

Script to stream the combination to YouTube:
```bash
#!/bin/bash
KEY=$1
echo
echo Cleanup -------------------------------------------------
source live-yt.key
killall -9 ffmpeg
killall -9 chromium
sleep 3

echo Setup General--------------------------------------------
cd /home/[USER]
rm -f nohup.out
export DISPLAY=:0.0
export $(dbus-launch)

echo Setup Chromium-------------------------------------------
CHROMIUM_TEMP=/home/{USER]/tmp/chromium
rm -rf $CHROMIUM_TEMP.bak
mv $CHROMIUM_TEMP $CHROMIUM_TEMP.bak
mkdir -p $CHROMIUM_TEMP

echo Start Chromium ------------------------------------------
nohup /usr/lib/chromium/chromium \
    --new-window "http://[ustreamerURL]" \
    --start-fullscreen \
    --disable \
    --disable-translate \
    --disable-infobars \
    --disable-suggestions-service \
    --disable-save-password-bubble \
    --disable-new-tab-first-run \
    --disable-session-crashed-bubble \
    --disable-bundled-ppapi-flash  \
    --disable-gpu \
    --enable-javascript \
    --enable-user-scripts \
    --disk-cache-dir=$CHROMIUM_TEMP/cache/ustreamer/ \
    --user-data-dir=$CHROMIUM_TEMP/user_data/ustreamer/ \
    --window-position=1440,12 \
     >/dev/null 2>&1 &
sleep 5

echo Start FFMpeg---------------------------------------------
nohup /usr/bin/ffmpeg \
    -loglevel level+warning \
    -thread_queue_size 512 \
    -framerate 30 \
    -f x11grab \
    -s 1920x1080 \
    -probesize 42M \
    -i :0.0+1024,0 \
    -i http://[VLCaudioURL] \
    -filter:a "volume=10, highpass=f=300, lowpass=f=2800" \
    -c:v libx264 \
    -pix_fmt yuv420p \
    -g 60 \
    -b:v 2500k \
    -c:a libmp3lame \
    -ar 44100 \
    -b:a 32k \
    -preset ultrafast \
    -maxrate 5000k \
    -bufsize 2500k \
    -preset ultrafast \
    -flvflags no_duration_filesize  \
    -f flv "rtmp://a.rtmp.youtube.com/live2/$KEY" \
     >/home/{USER]/ff-audio.log 2>&1 &

echo Done ----------------------------------------------------
echo
```

*PS: Recipe by David Klippel*
