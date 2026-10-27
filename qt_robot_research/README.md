# 救援机器人 Qt/C++ 界面：GitHub 项目选型

核查日期：2026-10-27。当前阶段为开源项目检索、README/许可证/关键源码核查和本地环境检查，尚未完成机器人界面，也未编译运行候选 GUI。

## 需求与建议

根据提供的项目截图，界面主要展示：视频检测结果、目标距离、设备状态，并通过 TCP 接收数据和发送控制参数。截图还涉及 Jetson Xavier NX、OpenCV 和 Livox MID-70；这些是真实设备接入的后续需求，不能由一个模拟界面代替。

建议使用 **C++17 + Qt Widgets + CMake**，先在 WSL Ubuntu 中开发。第一版可组织为：左侧视频与检测框，右侧目标类别/置信度/距离表、摄像头/LiDAR/Jetson/TCP 状态，顶部连接参数，底部日志。三维点云窗口可作为后续模块。

## 已筛选的 GitHub 项目

### 1. RadarSimulator：优先参考通信、目标表和状态显示

- 仓库：https://github.com/cyclone367/RadarSimulator
- 许可证：[MIT](https://github.com/cyclone367/RadarSimulator/blob/master/LICENSE)。
- 技术栈：C++17、Qt6 Widgets/Network、CMake；无 ROS/PCL/VTK 依赖。
- 已核查实现：TCP 服务端、多客户端独立接收缓冲、CRLF 文本分帧、命令响应、目标距离/方位/状态表、状态灯、二维雷达绘制和选点联动。
- 证据：[RadarServer.cpp](https://github.com/cyclone367/RadarSimulator/blob/master/RadarServer.cpp)、[LineBuffer.hpp](https://github.com/cyclone367/RadarSimulator/blob/master/shared/LineBuffer.hpp)、[mainwindow.cpp](https://github.com/cyclone367/RadarSimulator/blob/master/mainwindow.cpp)。
- 限制：它是软件雷达模拟器，没有摄像头、YOLO 或 MID-70 数据接入，也不是三维激光点云查看器。现有协议不能原样视为机器人协议。
- 构建限制：[CMakeLists.txt](https://github.com/cyclone367/RadarSimulator/blob/master/CMakeLists.txt) 要求 Qt >=6.5；本机当前 Ubuntu 软件源提供 Qt 6.4.2，需适配构建/API 或另装满足要求的 Qt，不能声称下载即能运行。
- 本地已克隆原始源码至 `sources/RadarSimulator/`，未修改，提交为 `dcd7cd0106074cb86c378a448a7cdf5df1dc62d6`。

### 2. ElaWidgetTools：可选的 C++ 界面美化组件库

- 仓库：https://github.com/Liniyous/ElaWidgetTools
- 许可证：[MIT](https://github.com/Liniyous/ElaWidgetTools/blob/main/LICENSE)。
- 已核查实现：Qt Widgets 的导航窗口、按钮、表格、卡片、主题、停靠窗口等；示例入口为 C++ QApplication。
- 用途：提供界面外观与通用组件，机器人通信、检测结果、测距逻辑仍需编写。
- 版本注意：README 对支持范围的描述比根目录 CMake 宽泛。当前 [CMakeLists.txt](https://github.com/Liniyous/ElaWidgetTools/blob/main/CMakeLists.txt) 排除 Qt >6.7.0，以及 6.5.3~6.6.1，并强制写入作者的 `D:/Qt/5.15.2/msvc2019_64` 路径；Linux 使用前需要处理路径。README 对 Linux 要求 Qt >=5.15。
- 建议：第一版也可以直接用 Qt 原生控件与样式表，减少第三方依赖；以后再决定是否引入 Ela。

### 3. RoboticUserInterface：较完整但较重的机器人上位机参考

- 仓库：https://github.com/ADrownFish/RoboticUserInterface
- 许可证：仓库根目录为 MIT。
- 源码已有机器人数据面板、曲线和多种通信相关模块；Communicator.cpp 中存在 QTcpSocket 客户端和 QTcpServer 收发路径。
- 文档偏差：README 仍将 TCP 标为 TODO，不能据此断言当前源码没有 TCP；也不能仅凭源码存在就声称运行稳定。
- 依赖：README 要求 Qt >6.9，另有 FluentUI、qcustomplot、Eigen、Bluetooth、DataVisualization 等，较适合拆模块参考。
- 对当前 WSL 环境：不作为第一版直接编译的首选。

### 4. Livox-SDK：MID-70 采集后端，非 Qt 界面项目

- 仓库：https://github.com/Livox-SDK/Livox-SDK
- 许可证：[LICENSE.txt](https://github.com/Livox-SDK/Livox-SDK/blob/master/LICENSE.txt) 对核心与示例列出 MIT，其他第三方组件按该文件分别列示。
- 官方 C/C++ SDK，提供 LiDAR 配置与点云接收接口。`livox_def.h` 明确存在 `kDeviceTypeLidarMid70`。
- LiDAR 到 SDK 使用 UDP；截图中的 TCP 可以用于 Jetson 与上位机之间的数据/控制通信，两者是不同链路。
- 后续可由 Jetson 采集和推理，再向 Qt 界面发送结果；尚未验证实物设备。

## 点云与标定补充参考

- [reefactor/ply_viewer](https://github.com/reefactor/ply_viewer)：根 LICENSE 为 MIT，Qt5/QOpenGLWidget 的轻量点云显示参考。读取静态 ASCII PLY，不能直接当作实时 Livox 查看器；旧 qmake 路径和 Qt6/CMake 适配需处理。作者说明测距拾取存在不足，因此不要复用它来承诺准确测距。
- [Livox 官方相机雷达标定](https://github.com/Livox-SDK/livox_camera_lidar_calibration)：可作标定和投影流程参考，不是 Qt 上位机。README 验证型号列为 Mid-40、Horizon、Tele-15，不能据此声称已在 MID-70 验证；还依赖 ROS/PCL/Eigen/Ceres 等。

## 本轮不建议直接作为底座的项目

- [chengyangkj/Ros_Qt5_Gui_App](https://github.com/chengyangkj/Ros_Qt5_Gui_App)：机器人图像、地图、状态显示贴近需求，但 README 写 MIT，根 LICENSE 为 GPL-2.0，存在许可描述冲突；还引入 ROS 等依赖。二维 LaserScan 显示不等于 MID-70 三维点云显示。
- [nightn/CloudViewer](https://github.com/nightn/CloudViewer)：有 Qt/PCL 点云查看功能，但根 LICENSE 为 Apache-2.0，中文手册又写有禁止商业使用的声明，存在描述冲突；使用旧 Qt/PCL/VTK 技术栈，当前阶段仅作为结构参考。
- 检索到的若干 YOLO + Qt GUI 实际由 PyQt/Python 编写。它们可以做外观参考，但不满足本次 C++ 实现要求。

## 当前 WSL 环境核查

本机实际检查结果：

- 使用发行版 `Ubuntu-24.04`。
- WSL 2.7.8.0，WSLg 1.0.73.2。
- `DISPLAY=:0`，`WAYLAND_DISPLAY=wayland-0`。
- Ubuntu 包管理器未安装 `qt6-base-dev` / `qtbase5-dev`；未排查所有可能的手动安装目录。
- 当前 APT 索引中，Qt6 开发包候选为 6.4.2，Qt5 开发包候选为 5.15.13。
- 具备运行 Linux GUI 的基础配置，但本轮尚未启动 Qt 窗口验证显示。

WSLg 能将 Linux GUI 窗口显示在 Windows 桌面，见 [Microsoft 官方说明](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps)。在 WSL 中构建得到的是 Linux 程序；如果后续需要原生 Windows exe，需要使用 Windows Qt 工具链另外构建。

第一版可用本地视频和模拟 TCP 数据完成显示链路。接入 USB 摄像头或局域网 LiDAR 时，需要分别验证 USB 透传、驱动和网络，而不能由 GUI 可显示推断硬件已接通。参见 [WSL USB](https://learn.microsoft.com/en-us/windows/wsl/connect-usb) 和 [WSL 网络](https://learn.microsoft.com/en-us/windows/wsl/networking)。

## Python 与 C++ 的选择

| 目标 | Python + PySide/PyQt | C++ + Qt Widgets |
|---|---|---|
| 快速拼出原型 | 通常更省代码，适合已有 Python 推理脚本 | 初始编译、类型和依赖配置工作更多 |
| 对接本项目的 OpenCV / Livox-SDK | 可以通过绑定或进程通信连接 | 可以直接衔接 C/C++ 接口 |
| 锻炼 Linux/C++ 与设备集成 | 覆盖有限 | 与当前项目路线更一致 |
| YOLO 推理性能 | 取决于实际推理后端、模型与数据链路 | 同样需要在目标硬件测量，不能只凭语言判断 |

[PySide6 是 Qt 官方 Python 绑定](https://doc.qt.io/qtforpython-6/index.html)，Python 方案本身完全可用。本项目建议继续采用 C++，理由是已有技术栈与目标平台衔接，而不是笼统认为 Python 做不了实时界面。

## 拟定实施顺序

1. 在 WSL 配置 Qt 开发包并验证最小窗口。
2. 制作视频区域、目标/距离表、设备状态、连接参数、日志区域。
3. 用明确标注的模拟数据验证交互与 TCP 协议。
4. 接入真实视频、检测结果与距离数据；最后进行真实设备联调。

以上是后续实施路线，不代表这些功能已经完成。
