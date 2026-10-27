#include "CameraController.h"

#include <QCameraFormat>
#include <QMediaDevices>
#include <QStringConverter>
#include <QTextStream>
#include <QVideoFrame>
#include <QVideoFrameFormat>

#include <algorithm>

CameraController::CameraController(QObject *parent)
    : QObject(parent)
{
    // 采集链路：camera -> session -> sink
    session_.setCamera(&camera_);
    session_.setVideoSink(&videoSink_);
    frameWatchdog_.setInterval(250);

    // 收到一帧视频：转成 QImage 后交给上层显示
    QObject::connect(&videoSink_, &QVideoSink::videoFrameChanged, this,
        [this](const QVideoFrame &frame) {
            if (!captureRequested_ || !frame.isValid())
                return;
            const QImage image = frame.toImage();
            if (image.isNull())
                return;
            // 让像素数据独立于采集缓冲区。这份 QImage 也是将来交给推理线程的对象。
            const QImage owned = image.copy();
            ++frameCount_;
            everReceivedFrame_ = true;
            frameClock_.restart();
            if (!receivedFrameThisRun_) {
                receivedFrameThisRun_ = true;
                QTextStream output(stdout);
                output.setEncoding(QStringConverter::Utf8);
                output << "FRAME " << owned.width() << ' ' << owned.height() << '\n';
                output.flush();
            }
            emit frameReady(owned);
        });

    // 8 秒没收到帧就判定采集失败
    QObject::connect(&frameWatchdog_, &QTimer::timeout, this, [this] {
        if (!captureRequested_ || !frameClock_.isValid() || frameClock_.elapsed() < 8000)
            return;
        const QString message = receivedFrameThisRun_
            ? QStringLiteral("已连续 8 秒没有收到视频帧，预览已停止。请检查设备连接。")
            : QStringLiteral("8 秒内未收到有效视频帧，已停止。请检查 USB 透传、设备占用和驱动。");
        QTextStream errors(stderr);
        errors.setEncoding(QStringConverter::Utf8);
        errors << "NO_FRAMES " << message << '\n';
        errors.flush();
        stop(message);
    });

    QObject::connect(&camera_, &QCamera::errorOccurred, this,
        [this](QCamera::Error error, const QString &message) {
            if (error == QCamera::NoError)
                return;
            const QString details = message.isEmpty() ? QStringLiteral("未知摄像头错误") : message;
            QTextStream errors(stderr);
            errors.setEncoding(QStringConverter::Utf8);
            errors << "CAMERA_ERROR " << details << '\n';
            errors.flush();
            stop(QStringLiteral("摄像头错误：%1").arg(details));
        });

    QObject::connect(&camera_, &QCamera::activeChanged, this, [this](bool active) {
        if (active && captureRequested_ && !receivedFrameThisRun_)
            emit waitingForFirstFrame();
        else if (!active && captureRequested_)
            stop(QStringLiteral("摄像头已停止输出，旧画面已清空。"));
    });

    QObject::connect(&mediaDevices_, &QMediaDevices::videoInputsChanged, this, [this] {
        if (!captureRequested_) {
            emit deviceListChanged();
            return;
        }
        // 采集过程中只关心当前设备是否被拔出；新插入的设备等停止后再刷新列表
        const QByteArray currentId = camera_.cameraDevice().id();
        const auto devices = QMediaDevices::videoInputs();
        const bool stillPresent = std::any_of(devices.cbegin(), devices.cend(),
            [&currentId](const QCameraDevice &device) { return device.id() == currentId; });
        if (!stillPresent)
            stop(QStringLiteral("正在使用的摄像头已断开，预览已停止。请重新连接后刷新。"));
    });
}

CameraController::~CameraController()
{
    // 先断开回调，再停止摄像头，避免销毁过程中信号打到半死的对象
    QObject::disconnect(&camera_, nullptr, this, nullptr);
    QObject::disconnect(&videoSink_, nullptr, this, nullptr);
    QObject::disconnect(&mediaDevices_, nullptr, this, nullptr);
    frameWatchdog_.stop();
    camera_.stop();
    session_.setVideoSink(nullptr);
    session_.setCamera(nullptr);
}

bool CameraController::start(const QByteArray &deviceId)
{
    if (captureRequested_)
        return true;

    const auto devices = QMediaDevices::videoInputs();
    const auto selected = std::find_if(devices.cbegin(), devices.cend(),
        [&deviceId](const QCameraDevice &device) { return device.id() == deviceId; });
    if (selected == devices.cend())
        return false;

    frameCount_ = 0;
    receivedFrameThisRun_ = false;
    captureRequested_ = true;

    camera_.setCameraDevice(*selected);
    selectPreferredFormat(*selected);
    frameClock_.start();
    frameWatchdog_.start();
    camera_.start();
    return true;
}

void CameraController::stop(const QString &reason)
{
    captureRequested_ = false;
    frameWatchdog_.stop();
    camera_.stop();
    emit stopped(reason);
}

void CameraController::selectPreferredFormat(const QCameraDevice &device)
{
    // 优先使用设备自己声明的格式；适中的分辨率/帧率更利于 USB/IP 传输
    QCameraFormat preferredFormat;
    qint64 preferredPixels = 0;
    double preferredRate = 0;
    bool preferredJpeg = false;
    for (const QCameraFormat &format : device.videoFormats()) {
        const QSize resolution = format.resolution();
        const double rate = format.maxFrameRate();
        if (resolution.width() <= 0 || resolution.height() <= 0
            || resolution.width() > 640 || resolution.height() > 480
            || rate <= 0 || rate > 30.01)
            continue;
        const qint64 pixels = static_cast<qint64>(resolution.width()) * resolution.height();
        const bool isJpeg = format.pixelFormat() == QVideoFrameFormat::Format_Jpeg;
        if (preferredFormat.isNull() || (isJpeg && !preferredJpeg)
            || (isJpeg == preferredJpeg && (pixels > preferredPixels
                || (pixels == preferredPixels && rate > preferredRate)))) {
            preferredFormat = format;
            preferredPixels = pixels;
            preferredRate = rate;
            preferredJpeg = isJpeg;
        }
    }
    if (preferredFormat.isNull())
        return;
    camera_.setCameraFormat(preferredFormat);
    QTextStream output(stdout);
    output.setEncoding(QStringConverter::Utf8);
    output << "SELECTED_FORMAT " << preferredFormat.resolution().width() << 'x'
           << preferredFormat.resolution().height() << ' '
           << QVideoFrameFormat::pixelFormatToString(preferredFormat.pixelFormat()) << '\n';
    output.flush();
}
