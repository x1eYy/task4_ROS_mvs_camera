# 海康 MV-CA016-10UC 相机 ROS 2 功能包

本仓库将海康机器人 **MV-CA016-10UC（USB3 彩色面阵相机）** 的 MVS C SDK 封装为 ROS 2 Humble 节点。节点按序列号发现相机，发布标准 `sensor_msgs/msg/Image`，支持曝光、增益、帧率、像素格式的动态设置，并在断线后自动连接原设备、恢复已接受的配置。

本机已使用一台 MV-CA016-10UC 实测。逐项结果见 [验收记录](docs/acceptance.md)。请勿将 MVS 厂商库或现场图像提交到 Git 仓库。

## 环境与编译

- Ubuntu 22.04 x86_64、ROS 2 Humble、`colcon`。
- 海康 MVS Linux x86_64 SDK。从[海康机器人官方下载中心](https://www.hikrobotics.com/en/machinevision/service/download/)选择 MVS 的 Linux x86_64 安装包，按厂商包内安装说明安装；安装后确认头文件和主库存在。本机安装于 `/opt/MVS`，头文件为 `include/MvCameraControl.h`，主库为 `lib/64/libMvCameraControl.so`，实测主库文件版本 4.8.2.1。其他 SDK 版本需自行回归测试。MVS **不能由 rosdep 安装**。
- ROS 依赖均写在 `package.xml`，可由 rosdep 检查或安装。

在仓库根目录运行：

```bash
source /opt/ros/humble/setup.bash            # zsh 改用 setup.zsh
rosdep install --from-paths src --ignore-src -r -y
colcon build --packages-select hikrobot_mvs_camera --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash                     # zsh 改用 setup.zsh
```

SDK 不在默认目录时，编译命令增加 `-DMVS_ROOT=/绝对路径/MVS`，启动时增加 `mvs_lib_dir:=/绝对路径/MVS/lib/64`。launch 会把该目录加入相机进程的 `LD_LIBRARY_PATH`，供 MVS 在运行时加载像素转换库。直接使用 `ros2 run ... camera_node` 时，需先手动设置同一环境变量。

## 连接与运行

相机应接在 **USB3 端口**，用 `lsusb -t` 确认链路为 `5000M`。接在 USB2（`480M`）时仍可取图，但本次实测帧率降至约 11.7 fps。关闭 MVS 桌面软件后再启动节点，避免相机独占访问冲突。

```bash
ros2 run hikrobot_mvs_camera camera_probe       # 列出型号、序列号、分辨率和参数范围
ros2 launch hikrobot_mvs_camera camera.launch.py serial_number:=你的序列号
ros2 topic hz /image_raw
ros2 topic echo /hikrobot_mvs_camera/actual_fps
rviz2 -d install/hikrobot_mvs_camera/share/hikrobot_mvs_camera/config/camera.rviz
```

只有一台 MV-CA016-10UC 可见时可不填 `serial_number`；多台同型号相机同时可见时必须指定。重连始终匹配原序列号。图像默认发布到 `/image_raw`，使用传感器数据 QoS；`actual_fps` 每约 5 秒发布一次。图像时间戳为主机收到该帧时的 ROS 时间，并非已同步的相机硬件时间。

| launch 参数 / ROS 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `serial_number` | 空 | 相机序列号，启动后不可修改 |
| `image_topic` | `/image_raw` | 图像话题，启动后不可修改 |
| `frame_id` | `camera_optical_frame` | 图像坐标系标识，启动后不可修改 |
| `exposure_time_us` | `-1.0` | 曝光时间，微秒；-1 表示启动时保留相机当前值 |
| `gain_db` | `-1.0` | 增益，dB；-1 表示启动时保留相机当前值，0 dB 有效 |
| `frame_rate_hz` | `-1.0` | 相机设置帧率；-1 表示启动时保留相机当前值 |
| `pixel_format` | 空 | MVS 像素格式符号；空表示启动时保留相机当前值 |
| `mvs_lib_dir` | `/opt/MVS/lib/64` | 仅供 launch 设置 SDK 运行时库路径 |

动态设置示例，每次设置一项：

```bash
ros2 param set /hikrobot_mvs_camera exposure_time_us 5000.0
ros2 param set /hikrobot_mvs_camera gain_db 2.0
ros2 param set /hikrobot_mvs_camera frame_rate_hz 30.0
ros2 param set /hikrobot_mvs_camera pixel_format BayerRG8
ros2 param get /hikrobot_mvs_camera pixel_format
```

节点先查询设备支持范围与格式，检查 SDK 返回值并读回确认；不支持或越界的值会被拒绝并给出原因。手动曝光/增益会关闭对应自动模式，设置帧率会启用帧率控制。像素格式切换会暂停并恢复采集。断线期间不接受新的动态参数；重新连接后恢复此前成功设置的参数。设置帧率与实际采集帧率是两个不同指标，例如曝光 50 ms 时，即使请求 30 Hz，实际帧率也可能约为 20 fps。

设备支持 11 种像素格式，均已实测能发布 ROS 图像：`Mono8`、`Mono10`、`Mono12`、`RGB8Packed`、`YUV422_YUYV_Packed`、`YUV422Packed`、`BayerRG8`、`BayerRG10`、`BayerRG10Packed`、`BayerRG12`、`BayerRG12Packed`。单色格式统一发布 `mono8`，彩色格式转换为 `bgr8`。相机报告的参数范围随固件与工作模式变化，以 `camera_probe` 的实时查询为准。

## 测试与排障

```bash
colcon test --packages-select hikrobot_mvs_camera
colcon test-result --verbose
rosdep check --from-paths src --ignore-src
```

假设备测试覆盖设备唯一选择、图像消息、动态参数拒绝及断线后配置恢复；实机结果见验收记录。若相机未被发现，检查 USB 供电、线缆、权限和 MVS 软件是否占用设备。若图像帧率偏低，先用 `lsusb -t` 排除 USB2 链路，再分别检查曝光时间、设置帧率与 `actual_fps`。若直接运行节点出现 `MV_CC_ConvertPixelTypeEx` 的动态库加载错误，使用上述 launch 或设置 `LD_LIBRARY_PATH`。
