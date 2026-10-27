# USB 摄像头预览：C++ / Qt 6

本轮只做摄像头画面预览。设备选择、刷新、打开、停止均在一个完整的 `main.cpp` 中，便于从头阅读。

显示链路：`QCamera → QMediaCaptureSession → QVideoWidget`。不需要 OpenCV、Python、检测模型或 TCP 服务。

## 在 WSL Ubuntu 中运行

依赖：Qt 6 Widgets / Multimedia / MultimediaWidgets、CMake、C++17 编译器。

```bash
sudo apt install qt6-base-dev qt6-multimedia-dev gstreamer1.0-plugins-good
cd /mnt/c/Users/zjp/Documents/ChatGPT/C++/usb_camera_viewer
bash run_wsl.sh
```

构建输出放在 Linux 文件系统的 `~/.cache/usb_camera_viewer-build/`。点击“刷新设备”，选择摄像头后点击“打开摄像头”。停止预览会释放摄像头并显示占位画面。

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
