# MV-CA016-10UC 实机验收记录

依据仓库外的《DX-ROS教程_修订版.pdf》末尾“任务”章节逐项核对。测试环境为 Ubuntu 22.04、ROS 2 Humble、MVS SDK 主库 4.8.2.1；设备为 USB3 彩色相机 MV-CA016-10UC，序列号在公开记录中遮盖为 `00D****1056`，固件 `V1.4.8 181031,18053001`。本仓库只针对这一型号。

| PDF 要求 | 实机或代码核验结果 |
| --- | --- |
| SDK 自动发现、按 IP 或序列号连接 | 通过 USB3 枚举自动发现；单台自动连接，多台必须给出序列号，重连固定原序列号。此型号为 USB3，无 IP 地址，故 IP 选择不适用。 |
| 断线重连及恢复参数 | 实际拔插 USB 后恢复同一相机的采集；恢复前设置的曝光 5000 µs、增益约 2 dB、帧率 10 Hz、`BayerRG8`，重连后取流约 10 fps，SDK 查询到相应配置。 |
| 默认分辨率下稳定取图、尽量高帧率 | USB3 `5000M`、1440×1080、出厂设置帧率 70.4 Hz 下，ROS 发布与 `actual_fps` 约 70.4 fps，观测窗口日志中 `frame_lost_packets=0`。接到 USB2 `480M` 时约 11.7 fps，故验收需使用 USB3。该值是本机实测，不保证其他主机相同。 |
| 标准图像消息、可配置 Topic | `/image_raw` 的类型为 `sensor_msgs/msg/Image`；改为 `/ca016/image_raw` 后正常发布。默认 YUV422 格式转换为 `bgr8`，1440×1080。 |
| 动态曝光与增益 | `ros2 param set` 运行时生效，设置前查询范围、关闭对应自动模式、检查 SDK 返回并读回。曝光 10→50 ms、增益 0→8 dB 的采集画面亮度有明显变化；越界请求被拒绝并返回原因。实机 `ros2 param get` 读取到 `current_exposure_time_us=10000`、`current_gain_db=0`、`current_frame_rate_hz≈70.4`、`current_pixel_format=YUV422_YUYV_Packed`；曝光改为 12000 µs 后读回 12000，尝试写只读读回参数被拒绝。 |
| 设置帧率与实际帧率分离 | 设置为 30 Hz、曝光 10 ms 时实测约 30 fps；曝光 50 ms 时实测约 20 fps，说明 `actual_fps` 为真实发布速度而非配置值。 |
| 动态像素格式 | 查询设备支持列表、停流切换、读回确认、重新采集。设备枚举出的 11 种格式均逐个设置并验证收到有效图像；单色为 `mono8`，彩色为 `bgr8`。不支持格式被拒绝。 |
| ROS 包、README、launch | `src/hikrobot_mvs_camera` 含 C++ 源码、`package.xml`、`CMakeLists.txt`、`camera.launch.py`、RViz2 配置及测试；README 说明 SDK 独立安装、版本和库路径。 |
| 编译、依赖、测试 | `colcon build --packages-select hikrobot_mvs_camera` 成功，无错误或严重警告；`colcon test` 为 3 项通过；`rosdep check --from-paths src --ignore-src` 显示系统依赖齐全。退出路径修复后连续 50 次启动/退出无崩溃。 |
| RViz2 图像验收 | 使用随包安装的 `config/camera.rviz`，在 Image 显示器中看到 `/image_raw` 实时图像，画面正常。 |

设备支持的 11 种像素格式：`Mono8`、`Mono10`、`Mono12`、`RGB8Packed`、`YUV422_YUYV_Packed`、`YUV422Packed`、`BayerRG8`、`BayerRG10`、`BayerRG10Packed`、`BayerRG12`、`BayerRG12Packed`。设备当前报告的范围约为曝光 16–9,999,710 µs、增益 0–17.0166 dB、帧率 0.1–100,000 Hz；后两者及实际可达值受相机工作模式、曝光、传输带宽约束，应以 `camera_probe` 的现场读数和 `actual_fps` 为准。

再次验收的命令见 [README](../README.md)。为保护现场信息，原始相机图像和未遮盖序列号不纳入公开仓库。图像时间戳使用主机 ROS 接收时间，并非硬件同步时间。
