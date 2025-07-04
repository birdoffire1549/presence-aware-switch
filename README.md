# presence-aware-switch (ProxiSwitch)
This is firmware for ESP-32 for use as a presence aware switch using Bluetooth LE Beacon.

The final device once assembled is a box with a power cord, an electrical outle, a push button and two LEDs.
When one places a BlueTooth LE beacon near the device, one of the LEDs lights up indicating compatible device in close range. This is known as the close device LED. 
This LED is a great way of being sure the device sees a device prior to it being learned and tracked. It is also a way for the device to indicate that something else is 
close enought that its signal may interfere with the learning of a desired device.

To learn or start tracking a beacon, the beacon should be placed as close as possible to the device, then the button on the device is pressed and held for about 5 or 6 seconds, 
at which time the Learning LED will turn on. At that time release the button and leave the beacon near the device. The LED will go out once loearning is complete. This takes about
10 seconds.

After a beacon has been learned, when it is near the device, the device will power on the box's outlet. When the beacon is far from the device and not seen in more than 1 minute the
device will power off the outlet until it next sees the beacon again.

To cause the device to forget a beacon that it has learned you can simply learn a new beacon by repeating the prior process, or you can press and hold the button for about 30 seconds
until the Learn LED starts flashing. Then release the button and the device will factory reset forgetting any tracked beacon so that no beacons can cause the outlet to turn on.
