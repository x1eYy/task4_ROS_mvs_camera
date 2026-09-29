# Hikrobot MVS ROS 2 camera

ROS 2 Humble C++ wrapper for one Hikrobot industrial camera. The eventual target is one
specific CS016 or CA016 variant; its full suffix and hardware capabilities have not yet
been supplied. The current code discovers GigE and USB3 Vision devices through
the MVS SDK and deliberately does not assume a resolution, frame rate, or color mode.

## Requirements and build

- Ubuntu 22.04, ROS 2 Humble, `colcon`, and ROS dependencies from `package.xml`.
- Hikrobot MVS **Linux x86_64 C SDK**, installed separately. This machine has MVS under
  `/opt/MVS`, with `/opt/MVS/include/MvCameraControl.h` and
  `/opt/MVS/lib/64/libMvCameraControl.so`. The observed library file is version 4.8.2.1.
  MVS is a vendor SDK and is **not** installed by `rosdep`.

From this repository root:

```bash
source /opt/ros/humble/setup.bash   # use setup.zsh in zsh
rosdep install --from-paths src --ignore-src -r -y
colcon build --packages-select hikrobot_mvs_camera --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash           # use setup.zsh in zsh
```

For a non-default MVS location, pass `-DMVS_ROOT=/absolute/path/to/MVS` in the CMake
arguments. If the executable cannot load an MVS library, add its 64-bit library directory
to the system loader configuration or `LD_LIBRARY_PATH`. Do not copy the vendor library
into this Git repository. The executable's installed RPATH points to the configured SDK
library directory on the build machine.

## Run and inspect

Connect exactly one camera, or provide a selector. `camera_ip` is for GigE only; a serial
number works for both interfaces. Simultaneously setting both selectors is an error.
An empty selector is accepted only if exactly one GigE/USB3 camera is discovered.

```bash
ros2 launch hikrobot_mvs_camera camera.launch.py serial_number:=YOUR_SERIAL
# or for GigE:
ros2 launch hikrobot_mvs_camera camera.launch.py camera_ip:=192.168.1.10
ros2 topic hz /image_raw
ros2 topic echo /hikrobot_mvs_camera/actual_fps
rviz2
```

In RViz2, add **Image** and choose `/image_raw`. The node publishes `mono8` for supported
monochrome formats and `bgr8` for supported Bayer/RGB formats. The timestamp is the ROS
host time when a frame is received; it is not a synchronized camera hardware timestamp.
Actual published FPS is measured over five-second windows, separately from the configured
camera frame rate. `frame_lost_packets` is logged from per-frame SDK metadata.

Launch arguments and node parameters:

| Name | Default | Meaning |
| --- | --- | --- |
| `serial_number` | empty | Exact device serial number |
| `camera_ip` | empty | Exact current GigE IPv4 address |
| `image_topic` | `/image_raw` | Published image topic |
| `frame_id` | `camera_optical_frame` | Image header frame ID |
| `exposure_time_us` | `-1.0` | Exposure in microseconds; `-1` keeps device setting at startup |
| `gain_db` | `-1.0` | Gain in dB; `-1` keeps device setting at startup |
| `frame_rate_hz` | `-1.0` | Requested camera frame rate; `-1` keeps device setting at startup |
| `pixel_format` | empty | MVS symbolic format, e.g. `Mono8`; empty keeps device setting |

The four camera settings can be changed while connected, one setting per request:

```bash
ros2 param set /hikrobot_mvs_camera exposure_time_us 5000.0
ros2 param set /hikrobot_mvs_camera gain_db 2.0
ros2 param set /hikrobot_mvs_camera frame_rate_hz 30.0
ros2 param set /hikrobot_mvs_camera pixel_format Mono8
ros2 param get /hikrobot_mvs_camera exposure_time_us
```

The SDK's supported range and format list govern accepted values. Manual exposure/gain
disables the corresponding auto mode, and frame-rate setting enables frame-rate control.
The node checks the SDK result and reads numeric values back; values beyond range or
unavailable on the camera are rejected with a reason. Supported pixel formats are limited
to Mono8/10/12/14/16, Bayer8/10/12/16, RGB8, and BGR8 variants that this conversion
path handles. Format changes stop and restart grabbing when necessary. Settings accepted
by the node are reapplied after reconnection. Parameter updates while disconnected are
rejected; startup values are retried on every successful connection.

## Hardware arrival checklist (not yet performed)

1. Record the **complete** model suffix, serial number, interface, and firmware version.
   Retain only that model's setup and acceptance instructions after identification.
2. Verify camera discovery and image acquisition in the vendor MVS application first.
   For GigE, check power, cable, NIC link, and that camera/NIC addresses share a subnet.
   For USB3, check the port, cable, and device permissions. Close the MVS application
   before starting this node because the node opens the camera exclusively.
3. Query the selected camera's default resolution, pixel formats, exposure/gain/frame-rate
   ranges, and rated maximum FPS. Record these in this README and set one model-specific
   launch default. Remove the unused interface's setup notes at that point.
4. Start the launch file with a serial number or GigE IP. Confirm `ros2 topic hz`,
   `/hikrobot_mvs_camera/actual_fps`, and the RViz2 Image display. Compare requested
   and actual FPS at the default resolution; for GigE inspect packet loss and tune the
   NIC and packet size if needed.
5. Set each of the four camera parameters; check SDK readback and visible image effects.
   Verify an out-of-range value fails with a clear reason. Unplug and reconnect the
   camera, then confirm the same Topic resumes and accepted settings are restored.
6. Re-run the build and tests; add one acceptance record containing commands, SDK and
   firmware versions, measured FPS, observed losses, and pass/fail results for **only
   the selected model**.

## Software tests

```bash
colcon test --packages-select hikrobot_mvs_camera
colcon test-result --verbose
```

The fake-device tests cover unique device selection, a valid `sensor_msgs/Image`, dynamic
parameter acceptance/rejection, and parameter restoration after a simulated disconnect.
They cannot establish real camera performance, SDK format support, or physical reconnect
behavior. Those remain hardware acceptance items above.
