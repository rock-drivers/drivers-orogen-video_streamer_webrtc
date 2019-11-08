# Video streaming using WebRTC

## IMPORTANT

As of today (2019/06/24 using Chrome 75 and GStreamer 1.14), the resolution of
ICE candidates that use .local addresses don't seem to work with GStreamer. The
workaround is to disable their usage completely in `chrome://flags`.

## Description

This component allows to stream an image feed using webrtc, using VP8 to
encode the stream.

There is very little knobs to configure: the fps rate (which allows to down-sample
the incoming stream) and the port to which the webrtc server is meant to connect.

The internal web service has stun disabled, which means that client and server
must be directly reachable (not through a NAT).

The `examples/run.html` file contains all you need to create a webrtc connection
on the browser side. You can run `examples/run.rb` under Syskit by going into
a valid Syskit app and do

~~~
syskit run <path to run.rb> -c
~~~

# License

This package is heavily based on the Raspberry Pi streaming code from
https://github.com/thaytan/gst-rpicamsrc/

This code being LGPLv2, this package is too.
