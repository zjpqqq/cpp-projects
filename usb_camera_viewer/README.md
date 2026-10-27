# USB 摄像头预览：C++ / Qt 6

本轮只做摄像头画面预览，不涉及目标检测。

## 代码结构

按职责分成两个类，界面与采集解耦（`include/` 放声明，`src/` 放实现）：

```
include/
  MainWindow.h        界面：布局、设备列表、画面显示
  CameraController.h  采集：QCamera / QVideoSink / 帧看门狗
src/
  main.cpp            程序入口：命令行参数与启动
  MainWindow.cpp
  CameraController.cpp
```

- `MainWindow` 不碰 Qt Multimedia，只接收 `CameraController` 的信号
- `CameraController` 不懂界面，只通过信号上报帧与状态；析构时自动停止采集（RAII）
- `main()` 只剩参数解析与启动

显示链路：`QCamera → QMediaCaptureSession → QVideoSink → QImage → QLabel`。不需要 OpenCV、Python、检测模型或 TCP 服务。

## 在 WSL Ubuntu 中运行

依赖：Qt 6 Widgets / Multimedia / MultimediaWidgets、CMake、C++17 编译器。

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev gstreamer1.0-plugins-good
cd /mnt/c/Users/zjp/Documents/ChatGPT/C++/usb_camera_viewer
cmake -S . -B ~/.cache/usb_camera_viewer-build -DCMAKE_BUILD_TYPE=Debug
cmake --build ~/.cache/usb_camera_viewer-build --parallel 2
~/.cache/usb_camera_viewer-build/usb_camera_viewer
```

构建输出放在 Linux 文件系统的 `~/.cache/usb_camera_viewer-build/`。点击“刷新”，选择摄像头后点击“打开”。停止预览会释放摄像头并显示占位画面。

## Windows 摄像头与 WSL 摄像头的区别

Windows 设置中能显示 USB Camera，只说明 Windows 能使用它。WSL 需要先通过 usbipd-win 接入，之后 Linux 才会出现 `/dev/video*`。

此机器已在 Windows 发现 USB Camera，硬件标识 `1bcf:2281`；当前 WSL 内尚未发现 `/dev/video*`。内核包含 `uvcvideo` 驱动模块。

首次安装 usbipd-win、共享 USB 设备需要 Windows 管理员权限。设备接入 WSL 期间不能同时由 Windows 相机或设置页面使用。请先关闭正在预览摄像头的 Windows 页面。

安装命令（Windows PowerShell）：

```powershell
winget install --interactive --exact dorssel.usbipd-win
usbipd list
```

在 `usbipd list` 中找到 `1bcf:2281` 的真实 BUSID，再按以下顺序操作；不要照抄示例设备编号：

```powershell
# 管理员 PowerShell，仅首次共享需要管理员权限
usbipd bind --busid <你的BUSID>

# 普通 PowerShell
usbipd attach --wsl --busid <你的BUSID>
```

回到 Ubuntu 检查：

```bash
ls -l /dev/video*
v4l2-ctl --list-devices
```

如果设备已出现但普通用户打不开，请检查 `/dev/video*` 权限和 `video` 用户组。不要通过长期以 root 运行界面解决权限问题。

结束使用后，在 Windows PowerShell 运行 `usbipd detach --busid <你的BUSID>`，即可恢复给 Windows 使用。拔插 USB 或重启 WSL 后可能需要重新 attach。

## 参考

- [Microsoft：在 WSL 连接 USB 设备](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)
- [usbipd-win：WSL support](https://github.com/dorssel/usbipd-win/wiki/WSL-support)
- [Qt：QCamera](https://doc.qt.io/qt-6/qcamera.html)
- [Qt：QVideoWidget](https://doc.qt.io/qt-6/qvideowidget.html)

编译、GUI 显示和实际摄像头取流是三个不同的验证阶段，当前状态请见 `verification.json`。
