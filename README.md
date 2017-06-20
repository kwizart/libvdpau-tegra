# About

This is a VDPAU backend driver for the NVIDIA Tegra20 SoC's.

# Requirements:

Mandatory:
* autotools
* libvdpau (https://www.freedesktop.org/wiki/Software/VDPAU/)
* pixman (http://www.pixman.org)
* libX11 libXext xextproto libXfixes libXv
* libdrm-tegra (https://github.com/grate-driver/libdrm)
* opentegra (https://github.com/grate-driver/xf86-video-opentegra)

Optional:
* mesa (https://github.com/grate-driver/mesa)
* Linux kernel Video Decoder Engine (VDE) driver, cherry-pick these patches to your kernel 
> https://github.com/digetx/picasso_upstream_support/commit/2b08ee544a5dd806697933e14c117e375ca29a2f
> https://github.com/digetx/picasso_upstream_support/commit/7f2036f832364a202356cedc54b2c4cc5c1a434c

The VDE linux kernel driver is optional, it is required for HW accelerated video decoding. Currently VDE driver
supports CAVLC H.264 videos only. The accelerated video output works without the VDE driver.

Usage of the most recent mainline upstream Linux kernel is very recommended, not all DRM fixes are backportable and some usability-critical features may be missed in older kernels.

# Installation:
```
$ sh autogen.sh
$ sh configure --prefix=/
$ make
$ make install
```

# Usage example:

```
$ VDPAU_DRIVER=tegra VDPAU_DRIVER_PATH=/path/to/libvdpau_tegra.so mpv --hwdec=vdpau --vo=vdpau video.mp4
```

The `VDPAU_DRIVER` and `VDPAU_DRIVER_PATH` environment variables aren't required if mesa (https://github.com/grate-driver/mesa) is installed.

You should see the following lines in the terminal:
```
Using hardware decoding (vdpau).
VO: [vdpau] 1280x720 vdpau[yuv420p]
```
If you don't see anything related to VDPAU, it means that it doesn't work for some reason. Check that `/usr/lib/vdpau/libvdpau_tegra.so.1` exists, note that `VDPAU_DRIVER_PATH` must point to the directory containing the shared library, not the library file.

Other players that support VDPAU are also (kinda) working, but mpv is recommended.

# Todo:

* ~~Accelerated output to overlay~~
* Offload ~~color conversion, blitting~~ and blending to HW
* H.264 CABAC support (reverse-engineering pending)
* Support other codecs, like VC-1 or H.263
