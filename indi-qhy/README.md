QHY CCD Driver
==============

This package provides QHY CCD/CMOS and Filter Wheels INDI driver.

Requirements
------------

+ INDI >= v2.1.0 (http://www.indilib.org)

	You need to install both indi and indi-devel to build this package.
	
+ cfitsio

	cfitsio-devel is required to compile support for FITS.
	
+ libusb-1

	libusb-1 is required.
	
+ libqhy

	libqhy SDK (located in ../libqhy/) is required.
	
+ fxload

	fxload that can upload FX3 EZUSB firmware files is required. Some distributions include an old version that do not support the fx3 flag.
	Compile a compatible version (https://github.com/lhondareyte/fxload.git) to support FX3.

+ nlohmann-json

	@nlohmann's JSON for Modern C++ library (https://github.com/nlohmann/json) is required to build this driver using CMake.
	It can be installed via APT as 'nlohmann-json3-dev' on Debian-based distros.

Installation
------------

	See INSTALL
	
How to Use
----------

	You can use the QHY INDI Driver in any INDI-compatible client such as KStars or Xephem. 
	
	To run the driver from the command line:
	
	$ indiserver indi_qhy_ccd
	
	You can then connect to the driver from any client, the default port is 7624.
	If you're using KStars, the driver will be automatically listed in KStars' Device Manager,
	no further configuration is necessary.

        If you cannot connect to the INDI driver from any INDI compliant application, run the QHY CCD test:

        $ qhy_ccd_test

        Share the test result output in INDI & QHY forums. Be as thorough as possible with your environment conditions (OS, architecture..etc)
	 
V4L2 Direct Backend (Linux)
---------------------------

The QHY V4L2 backend is intended for QHY camera pipelines exposed by a Linux
media-controller graph. A camera is instantiated only when its sensor device-tree
node explicitly provides `qhy,indi-name`. The optional `qhy,indi-role` property
is used only to append a display role such as `Main` or `Guide`.

The backend requires a complete sensor-to-capture media pipeline with both a
capture `/dev/video*` node and a sensor `/dev/v4l-subdev*` node. It uses standard
V4L2 controls where available for exposure, analogue gain, and black level.

Supported raw input is unpacked 8/10/12/14/16-bit Bayer or monochrome data stored
in a 16-bit container. Packed raw formats are currently rejected rather than
silently producing a corrupted image. The backend does not configure vendor or
board-specific sysfs controls.

Nicknames
---------

The `indi_qhy_ccd` driver uses the generalized INDI nickname scheme
introduced in [INDI PR #2343](https://github.com/indilib/indi/pull/2343).
Nicknames are stored in `~/.indi/INDINicknames.xml` in a format like the below,
and are assoicated with a driver and stable device identifier.

```
<INDINicknames>
 <nickname driver="AcmeFocuser" identifier="SN123">MainScope</nickname>
 <nickname driver="AcmeFocuser" identifier="SN456">GuideScope</nickname>
 <nickname driver="AcmeDustCap" identifier="CAP-1-2-3">MainScope</nickname>
</INDINicknames>
```

For the specific case of QHY cameras, the format looks something like this:
```
<INDINicknames>
  <device name="QHY CCD">
    <nickname identifier="QHY294PROC-4ce22a4b811ae7175">QHY294C UW</nickname>
  </device>
</INDINicknames>
```
where the device name is simply "`QHY CCD`", and the identifier is printed in
the `indiserver` output and log in the line "`*** This is the camera ID: [______]`".
