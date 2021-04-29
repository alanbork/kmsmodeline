Whether by design or by oversight, it turns out the kms/drm driver architecture used in linux to set the current video mode allows easy selection of custom modes as well. You just stuff the values you want into the mode structure returned by getConnector, and then tell drm to use that mode. Because this is close to the metal you can't just say height, width, and refresh, but need to specify the full timing info like you would using xorg's modelines.

This complete example takes a xorg compliant modeline and stuffs it into the proper fields of the mode structure. It has been tested on the raspberry pi 4. As a debugging tool, it can also dump the modelines of the modes detected by kms/linux for your monitor. 

disclaimer: there may well be more proper ways to do this, I haven't read the docs to see.

**Raspberry Pi 4 note**

The raspberry Pi 4, at the moment of writing this, has a limited KMS driver, and a buggy "fake" kms driver. This is because the GPU is different from the previous ones. Instead of using the `vc` libraries, you will need to use the DRM/GBM.

**What do I need?**

You need a GCC compiler, GDM, EGL, and GLES libraries. The GCC compiler is already included in the Raspbian image. To install the other libraries, simply run:

```bash
sudo apt-get install libegl1-mesa-dev libgbm-dev libgles2-mesa-dev
```

You will also need to connect your Raspberry Pi to a screen. The boot config from `/boot/config.txt` that I have used for my tests, if it helps in any way:

```bash
dtoverlay=vc4-fkms-v3d
max_framebuffers=2
hdmi_force_hotplug=1
hdmi_group=2
hdmi_mode=81
```

**How do I try it?**

Copy or download the `kmsmodeline.c` file onto your Raspberry Pi. Using any terminal, write the following commands to compile the source file:

```
gcc -o kmsmodeline kmsmodeline.c -ldrm -lgbm -lEGL -lGLESv2 -I/usr/include/libdrm -I/usr/include/GLES2
```

To run the executable, type the following:

```
kmsmodeline <mode number>
kmsmodeline <mode number> "xorg modeline", eg:
kmsmodeline 31 "13.514000 720 739 801 858 480 488 494 525 -hsync -vsync interlace dblclk"
```

You should see the following output:

```
% kmsmodeline 31 
/dev/dri/card0 does not have DRM resources, using card1
720x480i-60.00hz(60): 13.514000 720 739 801 858 480 488 494 525 -hsync -vsync interlace dblclk
Initialized EGL version: 1.4

```

The program will briefly show the following full screen:

4 seconds of warming up the screen to wait for the monitor to finish switching modes
60 frames of black white full screen flicker.

timing for each buffer flip, eg
````
16.69 ms        59.90 hz
16.64 ms        60.10 hz
16.69 ms        59.91 hz
16.64 ms        60.10 hz
[....]
````


## Troubleshooting and Questions

**Failed to get EGL version! Error:**

Your EGL might be faulty! Make sure you are using the libraries provided by Raspbian and not the ones installed through apt-get or other package managers. Use the ones in the `/opt/vc/lib` folder. If that does not work, try reinstalling your Raspberry Pi OS.

**I get "Error! The glViewport/glGetIntegerv are not working! EGL might be faulty!" What should I do?**

Same as above.

## License

Do whatever you want.

```
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
```
