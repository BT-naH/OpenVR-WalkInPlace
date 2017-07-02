![language](https://img.shields.io/badge/Language-C%2B%2B11-green.svg)  ![dependencies](https://img.shields.io/badge/Dependencies-Boost%201.63-green.svg)  ![license_gpl3](https://img.shields.io/badge/License-GPL%203.0-green.svg)

# OpenVR-InputEmulator - Step Detection Fork

An OpenVR driver that allows to create virtual controllers, emulate controller input, enable motion compensation manipulate poses of existing controllers and remap buttons. This fork of OpenVR-InputEmulator also includes Step Detection for WIP locomotion. Includes a dashboard to configure some settings directly in VR, a command line client for more advanced settings, and a client-side library to support development of third-party applications.

![Example Screenshot](https://raw.githubusercontent.com/matzman666/OpenVR-InputEmulator/master/docs/screenshots/InVRScreenshot.png)

The OpenVR driver hooks into the HTC Vive lighthouse driver and allows to modify any pose updates or button/axis events coming from the Vive controllers before they reach the OpenVR runtime. Due to the nature of this hack the driver may break when Valve decides to update the driver-side OpenVR API.

# Original motivation
The motivation of this driver is that I want to make myself a tracked gun that is guaranteed to work in any SteamVR game regardless of whether the original dev wants to support tracked guns or not. To accomplish this I need some way to add translation and rotation offsets to the poses of the motion controllers so that I can line up my tracked gun and the gun in the game. Additionally I need a way to easily switch between the tracking puck on my gun and my motion controller with the game thinking it's still the same controller (Throwing grenades with a tracked gun is not fun). But this driver should also support other use cases. 

There is also a client-side API which other programs can use to communicate with the driver. This API should be powerful enough to also support the development of full-fledged motion-controller drivers.

# Step Detection Motivation
I have done work with step detection locomotion with mobile phone VR games. So once I got my vive I was also interested in the options. Once I saw the functionality of the Original OpenVR-InputEmulator I linked the functionality to the ability to detect and "emulate" a step. In the long run I hope this functionality to provide a very seemless and pleasant WIP locomotion experience for those interested in trying the walking in place method.


# Features

- Step detection, WIP, locomotion controls
- Add translation and rotation offsets to the pose of existing controllers.
- Redirect the pose from one controller to another.
- Swap controllers.
- Motion  compensation for 6-dof motion platforms.
- Create virtual controllers and control their positions and rotations.
- Emulate controller input.
- Remap controller buttons.
- ...

# Notes:

This is a work-in-progress and may contain bugs.

# Usage

## Installer

Download the newest installer from the [release section](https://github.com/matzman666/OpenVR-InputEmulator/releases) and then execute it. Don't forget to exit SteamVR before installing/de-installing.

## Command-Line Client

Download the newest command-line client from the [release section](https://github.com/matzman666/OpenVR-InputEmulator/releases), unzip it, and then execute the contained binary in a cmd window. Enter *'client_commandline.exe help'* on the command line for usage instructions.


# Documentation

## Top Page:

![Root Page](https://raw.githubusercontent.com/matzman666/OpenVR-InputEmulator/master/docs/screenshots/DeviceManipulationPage.png)

- **Identify**: Sends a haptic pulse to the selected device (Devices without haptic feedback like the Vive trackers can be identified by a flashing white light).
- **Status**: Shows the current status of the selected device.
- **Device Mode**: Allows to select a device mode.
  - **Default**: Default mode.
  - **Disable**: Let OpenVR think that the device has been disconnected.
  - **Redirect to**: Impersonate another device.
  - **Swap with**: Swap two devices.
  - **Motion Compensation**: Enable motion compensation with the selected device as reference device.
- **Device Offsets**: Allows to add translation or rotation offsets to the selected device.
- **Motion Compensation Settings**: Allows to configure motion compensation mode.
- **Render Model**: Shows a render model at the device position (experimental).
- **Profile**: Allows to apply/define/delete device offsets/motion compensation profiles.

### Step Detection Mode
only one device needs to have "Step Detection" applied in the UI.
Then you simply walk in place to emulate walking in the VR world.

### Redirect Mode

Redirect mode allows to redirect the pose updates and controller events from one controller to another. To enable it select the device from with the pose updates/controller events should be redirected, then set the device mode to "Redirect to" and select the device that should be the redirect target from the combo box on the right, and at last press 'Apply'.

Redirect mode can be temporarily suspended by pressing the system button on either the source or target device.

## Device Offsets Page:

![Device Offsets Page](https://raw.githubusercontent.com/matzman666/OpenVR-InputEmulator/master/docs/screenshots/DeviceOffsetsPage.png)

- **Enable Offsets**: Enable/disable device offsets.
- **WorldFormDriver Offsets**: Allows to add offsets to the 'WorldFromDriver' transformations.
- **DriverFromHead Offsets**: Allows to add offsets to the 'DriverFromHead' transformations.
- **Driver Offsets**: Allows to add offsets to the device driver pose.
- **Clear**: Set all offsets to zero.

## Motion Compensation Settings Page:

![Motion Compensation Settings Page](https://raw.githubusercontent.com/matzman666/OpenVR-InputEmulator/master/docs/screenshots/MotionCompensationPage.png)

- **Vel/Acc Compensation Mode**: How should reported velocities and acceleration values be adjusted. The problem with only adjusting the headset position is that pose prediction also takes velocity and acceleration into accound. As long as the reported values to not differ too much from the real values, pose prediction errors are hardly noticeable. But with fast movements of the motion platform the pose prediction error can be noticeable. Available modes are:
  - **Disabled**: Do not adjust velocity/acceration values.
  - **Set Zero**: Set all velocity/acceleration values to zero. Most simple form of velocity/acceleration compensation.
  - **Use Reference Tracker**: Substract the velocity/acceleration values of the motion compensation reference tracker/controller from the values reported from the headset. Most accurate form of velocity/acceleration compensation. However, it requires that the reference tracker/controller is as closely mounted to the head position as possible. The further away it is from the head position the larger the error.
  - **Linear Approximation (Experimental)**: Uses linear approximation to estimate the velocity/acceleration values. The used formula is: (current_position - last_position) / time_difference, however the resulting values do cause a lot of jitter and therefore they are divided by four to reduce jitter to an acceptable level.

## client_commandline commands:

### listdevices

Lists all openvr devices.

### buttonevent

```
buttonevent [press|pressandhold|unpress|touch|touchandhold|untouch] <openvrId> <buttonId> [<pressTime>]
```

Emulates a button event on the given device. See [openvr.h](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h#L600-L626) for available button ids.

### axisevent

```
axisevent <openvrId> <axisId> <x> <y>
```

Emulates an axisevent on the given device. Valid axis ids are 0-4.

### proximitysensor

```
proximitysensor <openvrId> [0|1]
```

Emulates a proximity sensor event on the given device.

### getdeviceproperty

```
getdeviceproperty <openvrId> scan
```

Scans the given device for all available device properties.

```
getdeviceproperty <openvrId> <property> [int32|uint64|float|bool|string]
```

Returns the given device property. See [openvr.h](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h#L235-L363) for valid property ids.

### listvirtual

Lists all virtual devices managed by this driver.

### addcontroller

```
addcontroller <serialnumber>
```

Creates a new virtual controller. Serialnumber needs to be unique. When the command is successful the id of the virtual controller is written to stdout.

### publishdevice

```
publishdevice <virtualId>
```

Tells OpenVR that there is a new device. Before this command is called all device properties should have been set.

### setdeviceproperty

```
setdeviceproperty <virtualId> <property> [int32|uint64|float|bool|string] <value>
```

Sets the given device property. See [openvr.h](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h#L235-L363) for valid property ids.

### removedeviceproperty

```
removedeviceproperty <virtualId> <property>
```

Removes the given device property. See [openvr.h](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h#L235-L363) for valid property ids.

### setdeviceconnection

```
setdeviceconnection <virtualId> [0|1]
```

Sets the connection state of the given virtual device. Default value is disconnected.

### setdeviceposition

```
setdeviceposition <virtualId> <x> <y> <z>
```

Sets the position of the given virtual device.

### setdevicerotation

```
setdeviceposition <virtualId> <x> <y> <z>
```

setdevicerotation <virtualId> <yaw> <pitch> <roll>.

### devicebuttonmapping

```
devicebuttonmapping <openvrId> [enable|disable]
```

Enables/disables device button mapping on the given device.

```
devicebuttonmapping <openvrId> add <buttonId> <mappedButtonId>
```

Adds a new button mapping to the given device. See [openvr.h](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h#L600-L626) for available button ids.

```
devicebuttonmapping <openvrId> remove [<buttonId>|all]
```

Removes a button mapping from the given device. 


## Client API

ToDo. See [vrinputemulator.h](https://github.com/matzman666/OpenVR-InputEmulator/blob/master/lib_vrinputemulator/include/vrinputemulator.h).

# Command-Line Client Examples

## Create virtual controller

```
# Create virtual controller
client_commandline.exe addcontroller controller01 # Writes virtual device id to stdout (Let's assume it is 0)
# Set device properties
client_commandline.exe setdeviceproperty 0 1000	string	lighthouse
client_commandline.exe setdeviceproperty 0 1001	string	"Vive Controller MV"
client_commandline.exe setdeviceproperty 0 1003	string	vr_controller_vive_1_5
client_commandline.exe setdeviceproperty 0 1004	bool	0
client_commandline.exe setdeviceproperty 0 1005	string	HTC
client_commandline.exe setdeviceproperty 0 1006	string	"1465809478 htcvrsoftware@firmware-win32 2016-06-13 FPGA 1.6/0/0 VRC 1465809477 Radio 1466630404"
client_commandline.exe setdeviceproperty 0 1007	string	"product 129 rev 1.5.0 lot 2000/0/0 0"
client_commandline.exe setdeviceproperty 0 1010	bool	1
client_commandline.exe setdeviceproperty 0 1017	uint64	2164327680
client_commandline.exe setdeviceproperty 0 1018	uint64	1465809478
client_commandline.exe setdeviceproperty 0 1029	int32	2
client_commandline.exe setdeviceproperty 0 3001	uint64	12884901895
client_commandline.exe setdeviceproperty 0 3002	int32	1
client_commandline.exe setdeviceproperty 0 3003	int32	3
client_commandline.exe setdeviceproperty 0 3004	int32	0
client_commandline.exe setdeviceproperty 0 3005	int32	0
client_commandline.exe setdeviceproperty 0 3006	int32	0
client_commandline.exe setdeviceproperty 0 3007	int32	0
client_commandline.exe setdeviceproperty 0 5000	string	icons
client_commandline.exe setdeviceproperty 0 5001	string	{htc}controller_status_off.png
client_commandline.exe setdeviceproperty 0 5002	string	{htc}controller_status_searching.gif
client_commandline.exe setdeviceproperty 0 5003	string	{htc}controller_status_searching_alert.gif
client_commandline.exe setdeviceproperty 0 5004	string	{htc}controller_status_ready.png
client_commandline.exe setdeviceproperty 0 5005	string	{htc}controller_status_ready_alert.png
client_commandline.exe setdeviceproperty 0 5006	string	{htc}controller_status_error.png
client_commandline.exe setdeviceproperty 0 5007	string	{htc}controller_status_standby.png
client_commandline.exe setdeviceproperty 0 5008	string	{htc}controller_status_ready_low.png
# Let OpenVR know that there is a new device
client_commandline.exe publishdevice 0
# Connect the device
client_commandline.exe setdeviceconnection 0 1
# Set the device position
client_commandline.exe setdeviceposition 0 -1 -1 -1
```

## Map the grip-button to the trigger-button and vice-versa on the controller with id 3

```
client_commandline.exe devicebuttonmapping 3 add 2 33
client_commandline.exe devicebuttonmapping 3 add 33 2
client_commandline.exe devicebuttonmapping 3 enable
```

## Initial Setup
### Download the LeapMotion SDK
1. Goto https://developer.leapmotion.com/get-started
1. Click "Download Orion Beta"
1. Unzip the "LeapSDK" folder in the zip file into `OpenVR-InputEmulator/third-party/LeapSDK`

### Boost
1. Goto https://sourceforge.net/projects/boost/files/boost-binaries/1.63.0/
1. Download Boost 1.63 Binaries (boost_1_63_0-msvc-14.0-64.exe)
1. Install Boost into `OpenVR-InputEmulator/third-party/boost_1_63_0`
  
### Qt
1. Goto https://download.qt.io/official_releases/qt/5.7/5.7.0/
1. Download Qt 5.7.0
1. Run the Qt installer (I installed it to "c:\Qt")
1. Goto `OpenVR-InputEmulator\client_overlay`
1. Create `client_overlay.vcxproj.user` and paste the following into it:

```
<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="14.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <QTDIR>C:\Qt\5.7\msvc2015_64</QTDIR>
  </PropertyGroup>
</Project>
```

NOTE: Adjust the path the `msvc2015_64` folder in Qt to match your installation

Then run the `windeployqt.bat` if your system doesn't find the exe update the batch to the absolute path
in `{QT_INSTLATION_PATH}\5.7\msvc2015_64\bin\windeployqt.exe`

## Building
1. Open *'VRInputEmulator.sln'* in Visual Studio 2015.
2. Build Solution


# Known Bugs

- The shared-memory message queue is prone to deadlock the driver when the client crashes or is exited ungracefully.

# License

This software is released under GPL 3.0.
