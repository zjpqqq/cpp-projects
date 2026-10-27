// 摄像头采集控制器
// 封装 Qt Multimedia 的 QCamera / QMediaCaptureSession / QVideoSink 与帧看门狗。
// 上层（MainWindow）不直接操作采集链路，只接收信号。

#ifndef USB_CAMERA_VIEWER_CAMERA_CONTROLLER_H
#define USB_CAMERA_VIEWER_CAMERA_CONTROLLER_H

#include <QByteArray>
#include <QCamera>
#include <QCameraDevice>
#include <QElapsedTimer>
#include <QImage>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVideoSink>

class CameraController : public QObject
{
    Q_OBJECT

public:
    explicit CameraController(QObject *parent = nullptr);
    // RAII：析构时停止采集并断开采集链路，调用方无需手动清理
    ~CameraController() override;

    // 打开指定设备。返回 false 表示设备已不存在（调用方负责刷新设备列表）。
    // 之后的采集状态通过信号通知。
    bool start(const QByteArray &deviceId);
    void stop(const QString &reason);

    bool isCapturing() const { return captureRequested_; }
    bool hasEverReceivedFrame() const { return everReceivedFrame_; }
    quint64 frameCount() const { return frameCount_; }

signals:
    // 收到一帧；QImage 已独立持有像素数据，可直接保存或传给推理线程
    void frameReady(const QImage &image);
    // 采集结束：手动停止 / 摄像头错误 / 8 秒无帧 / 设备断开
    void stopped(const QString &message);
    // 摄像头已激活但还没收到首帧
    void waitingForFirstFrame();
    // 非采集状态下设备列表发生变化
    void deviceListChanged();

private:
    void selectPreferredFormat(const QCameraDevice &device);

    // 采集链路：按声明顺序构造，析构顺序相反（session_ 先析构）
    QCamera camera_;
    QVideoSink videoSink_;
    QMediaCaptureSession session_;
    QMediaDevices mediaDevices_;
    QTimer frameWatchdog_;
    QElapsedTimer frameClock_;

    bool captureRequested_ = false;
    bool receivedFrameThisRun_ = false;
    bool everReceivedFrame_ = false;
    quint64 frameCount_ = 0;
};

#endif // USB_CAMERA_VIEWER_CAMERA_CONTROLLER_H
