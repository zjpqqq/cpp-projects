#include <QApplication>
#include <QCamera>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QCommandLineParser>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <functional>

int main(int argc, char* argv[])
{
    // 1. QApplication owns the Qt GUI event loop. All code stays in main().
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("usb_camera_viewer"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QFont font;
    font.setFamilies({QStringLiteral("Noto Sans CJK SC"), QStringLiteral("Microsoft YaHei"),
        QStringLiteral("sans-serif")});
    font.setPointSize(10);
    QApplication::setFont(font);

    QTextStream output(stdout);
    QTextStream errors(stderr);
    output.setEncoding(QStringConverter::Utf8);
    errors.setEncoding(QStringConverter::Utf8);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("使用 Qt 原生接口预览 USB 摄像头，不包含模拟画面或目标检测。"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("list"), QStringLiteral("只列出 Qt 识别的摄像头，不打开摄像头或窗口。")});
    parser.addOption({QStringLiteral("auto-start"), QStringLiteral("窗口显示后自动打开默认摄像头。")});
    parser.addOption({QStringLiteral("quit-after"), QStringLiteral("在指定毫秒后退出，用于验证。"), QStringLiteral("ms")});
    parser.addOption({QStringLiteral("snapshot"), QStringLiteral("仅在没有摄像头且从未收到帧时保存窗口空状态截图。"), QStringLiteral("path")});
    parser.process(app);

    int quitAfterMs = 0;
    if (parser.isSet(QStringLiteral("quit-after"))) {
        bool valid = false;
        quitAfterMs = parser.value(QStringLiteral("quit-after")).toInt(&valid);
        if (!valid || quitAfterMs <= 0) {
            errors << "--quit-after 必须是大于 0 的毫秒整数。\n";
            return 2;
        }
    }
    const QString snapshotPath = parser.value(QStringLiteral("snapshot"));
    if (parser.isSet(QStringLiteral("snapshot")) && snapshotPath.trimmed().isEmpty()) {
        errors << "--snapshot 需要一个输出文件路径。\n";
        return 2;
    }

    if (parser.isSet(QStringLiteral("list"))) {
        const auto cameras = QMediaDevices::videoInputs();
        if (cameras.isEmpty()) {
            output << "NO_CAMERA WSL 尚未识别 USB 摄像头。\n";
        } else {
            for (qsizetype index = 0; index < cameras.size(); ++index) {
                output << "CAMERA " << index << ' ' << cameras[index].description();
                if (cameras[index].isDefault())
                    output << " [default]";
                output << '\n';
                for (const QCameraFormat& format : cameras[index].videoFormats()) {
                    output << "  FORMAT " << format.resolution().width() << 'x'
                           << format.resolution().height() << ' '
                           << QVideoFrameFormat::pixelFormatToString(format.pixelFormat())
                           << ' ' << format.minFrameRate() << '-' << format.maxFrameRate() << '\n';
                }
            }
        }
        output.flush();
        return 0;
    }

    // 2. Build a small Widgets window: device selector, video area, and status.
    QWidget window;
    window.setWindowTitle(QStringLiteral("USB 摄像头预览 · C++ / Qt"));
    window.resize(1000, 700);
    window.setMinimumSize(760, 560);
    window.setStyleSheet(QStringLiteral(R"(
        QWidget { background: #101823; color: #dce6ef; }
        QLabel#title { color: #f0f5fa; font-size: 23px; font-weight: 600; }
        QLabel#subtitle { color: #8fa2b5; }
        QLabel#status { background: #192633; border: 1px solid #2c4052;
                        border-radius: 6px; padding: 8px 12px; }
        QLabel#placeholder { color: #92a5b8; background: #080f18; }
        QLabel#videoImage { background: #080f18; }
        QLabel#metrics { color: #8fa2b5; }
        QComboBox { background: #1b2b3a; border: 1px solid #395166;
                    border-radius: 5px; padding: 7px 10px; min-height: 22px; }
        QComboBox QAbstractItemView { background: #1b2b3a; selection-background-color: #236b76; }
        QPushButton { background: #223649; border: 1px solid #395166;
                      border-radius: 5px; padding: 8px 17px; }
        QPushButton:hover { background: #2c485d; }
        QPushButton#open { background: #15777d; border-color: #23969b; color: white; }
        QPushButton#open:hover { background: #1b8a90; }
        QPushButton:disabled, QPushButton#open:disabled {
            background: #172330; color: #586c7e; border-color: #263b4c;
        }
    )"));

    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(10);
    auto* title = new QLabel(QStringLiteral("USB 摄像头预览"));
    title->setObjectName(QStringLiteral("title"));
    layout->addWidget(title);
    auto* subtitle = new QLabel(QStringLiteral("选择设备，查看实时摄像头画面。"));
    subtitle->setObjectName(QStringLiteral("subtitle"));
    layout->addWidget(subtitle);

    auto* controls = new QHBoxLayout;
    auto* deviceLabel = new QLabel(QStringLiteral("设备"));
    auto* deviceBox = new QComboBox;
    auto* refreshButton = new QPushButton(QStringLiteral("刷新"));
    auto* openButton = new QPushButton(QStringLiteral("打开"));
    auto* stopButton = new QPushButton(QStringLiteral("停止"));
    openButton->setObjectName(QStringLiteral("open"));
    controls->addWidget(deviceLabel);
    controls->addWidget(deviceBox, 1);
    controls->addWidget(refreshButton);
    controls->addWidget(openButton);
    controls->addWidget(stopButton);
    layout->addLayout(controls);

    // The panel stays 16:9; each QImage is fitted without changing its aspect
    // ratio. A normal QLabel avoids a native video child window under WSLg.
    auto* pages = new QStackedWidget;
    auto* placeholder = new QLabel;
    placeholder->setObjectName(QStringLiteral("placeholder"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    auto* videoImage = new QLabel;
    videoImage->setObjectName(QStringLiteral("videoImage"));
    videoImage->setAlignment(Qt::AlignCenter);
    videoImage->setMinimumSize(1, 1);
    videoImage->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    pages->addWidget(placeholder);
    pages->addWidget(videoImage);
    pages->setCurrentIndex(0);
    layout->addWidget(pages, 1, Qt::AlignCenter);

    auto* metrics = new QLabel(QStringLiteral("本次实际收到：0 帧    |    画面尺寸：—"));
    metrics->setObjectName(QStringLiteral("metrics"));
    layout->addWidget(metrics);
    auto* status = new QLabel;
    status->setObjectName(QStringLiteral("status"));
    status->setWordWrap(true);
    layout->addWidget(status);

    // Declare state before signal sources so state outlives those sources.
    QList<QCameraDevice> cameraDevices;
    bool captureRequested = false;
    bool receivedFrameThisRun = false;
    bool everReceivedFrame = false;
    quint64 frameCount = 0;
    QSize lastFrameSize;
    QImage latestFrame;
    QElapsedTimer frameClock;
    std::function<void(bool)> refreshDevices;
    std::function<void(const QString&)> stopCapture;

    // 3. Qt capture chain: camera -> session -> video sink -> QImage -> QLabel.
    // Session is constructed after camera/sink, so it is destroyed first.
    // No OpenCV or Python is involved in this display path.
    QMediaDevices mediaDevices;
    QCamera camera;
    QVideoSink videoSink;
    QMediaCaptureSession session;
    QTimer frameWatchdog;
    frameWatchdog.setInterval(250);
    session.setCamera(&camera);
    session.setVideoSink(&videoSink);

    auto displayLatestFrame = [&] {
        if (!latestFrame.isNull()) {
            videoImage->setPixmap(QPixmap::fromImage(latestFrame).scaled(
                pages->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    };

    refreshDevices = [&](bool updateStatus) {
        if (captureRequested)
            return;
        const QByteArray previousId = deviceBox->currentData().toByteArray();
        cameraDevices = QMediaDevices::videoInputs();
        const QSignalBlocker blocker(deviceBox);
        deviceBox->clear();
        int selectedIndex = -1;
        int defaultIndex = 0;
        for (qsizetype index = 0; index < cameraDevices.size(); ++index) {
            const QCameraDevice& device = cameraDevices[index];
            deviceBox->addItem(device.description(), device.id());
            if (device.id() == previousId)
                selectedIndex = static_cast<int>(index);
            if (device.isDefault())
                defaultIndex = static_cast<int>(index);
        }
        if (cameraDevices.isEmpty()) {
            deviceBox->addItem(QStringLiteral("没有可用摄像头"));
            placeholder->setText(QStringLiteral("WSL 尚未识别 USB 摄像头\n\n请先检查 USB 透传、UVC 驱动和 /dev/video*，然后点击刷新。"));
            if (updateStatus)
                status->setText(QStringLiteral("未发现视频输入设备。尚未打开摄像头，也未收到视频帧。"));
        } else {
            deviceBox->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : defaultIndex);
            placeholder->setText(QStringLiteral("摄像头就绪\n\n选择设备，点击“打开”开始预览。"));
            if (updateStatus)
                status->setText(QStringLiteral("已发现 %1 个摄像头；等待打开。\n").arg(cameraDevices.size()).trimmed());
        }
        deviceBox->setEnabled(!cameraDevices.isEmpty());
        refreshButton->setEnabled(true);
        openButton->setEnabled(!cameraDevices.isEmpty());
        stopButton->setEnabled(false);
    };

    stopCapture = [&](const QString& message) {
        captureRequested = false;
        frameWatchdog.stop();
        camera.stop();
        // Keep session -> sink connected across stop/restart. Clear our image
        // and show a normal Widgets placeholder instead of a frozen video.
        latestFrame = QImage();
        videoImage->clear();
        pages->setCurrentIndex(0);
        refreshDevices(false);
        placeholder->setText(message);
        status->setText(message);
    };

    auto startCapture = [&] {
        if (captureRequested)
            return;
        const QByteArray selectedId = deviceBox->currentData().toByteArray();
        const auto availableDevices = QMediaDevices::videoInputs();
        const auto selected = std::find_if(availableDevices.cbegin(), availableDevices.cend(),
            [&](const QCameraDevice& device) { return device.id() == selectedId; });
        if (selected == availableDevices.cend()) {
            refreshDevices(true);
            status->setText(QStringLiteral("所选摄像头不可用，请检查 USB 连接后刷新。"));
            return;
        }
        frameCount = 0;
        lastFrameSize = QSize();
        latestFrame = QImage();
        videoImage->clear();
        receivedFrameThisRun = false;
        captureRequested = true;
        metrics->setText(QStringLiteral("本次实际收到：0 帧    |    画面尺寸：—"));
        placeholder->setText(QStringLiteral("正在打开摄像头…\n\n等待第一帧，最长 8 秒。"));
        pages->setCurrentIndex(0);
        status->setText(QStringLiteral("正在启动摄像头，等待实际视频帧…"));
        deviceBox->setEnabled(false);
        refreshButton->setEnabled(false);
        openButton->setEnabled(false);
        stopButton->setEnabled(true);
        camera.setCameraDevice(*selected);
        // Prefer a format actually advertised by this device. Modest frame
        // size/rate helps USB/IP transport; never invent an unsupported mode.
        QCameraFormat preferredFormat;
        qint64 preferredPixels = 0;
        double preferredRate = 0;
        bool preferredJpeg = false;
        for (const QCameraFormat& format : selected->videoFormats()) {
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
        if (!preferredFormat.isNull()) {
            camera.setCameraFormat(preferredFormat);
            output << "SELECTED_FORMAT " << preferredFormat.resolution().width() << 'x'
                   << preferredFormat.resolution().height() << ' '
                   << QVideoFrameFormat::pixelFormatToString(preferredFormat.pixelFormat()) << '\n';
            output.flush();
        }
        frameClock.start();
        frameWatchdog.start();
        camera.start();
    };

    // 4. Every connection has a QObject context; UI updates stay in this thread.
    QObject::connect(refreshButton, &QPushButton::clicked, &window,
        [&] { refreshDevices(true); });
    QObject::connect(openButton, &QPushButton::clicked, &window, startCapture);
    QObject::connect(stopButton, &QPushButton::clicked, &window,
        [&] { stopCapture(QStringLiteral("预览已停止，旧画面已清空。")); });

    QObject::connect(&videoSink, &QVideoSink::videoFrameChanged, &window,
        [&](const QVideoFrame& frame) {
            if (!captureRequested || !frame.isValid())
                return;
            const QImage image = frame.toImage();
            if (image.isNull())
                return;
            // Own pixel storage independently of the capture buffer. This is
            // also the image to pass to a future inference worker.
            latestFrame = image.copy();
            ++frameCount;
            everReceivedFrame = true;
            lastFrameSize = latestFrame.size();
            frameClock.restart();
            if (!receivedFrameThisRun) {
                receivedFrameThisRun = true;
                pages->setCurrentIndex(1);
                status->setText(QStringLiteral("正在预览 USB 摄像头，已收到真实视频帧。"));
                output << "FRAME " << latestFrame.width() << ' ' << latestFrame.height() << '\n';
                output.flush();
            }
            displayLatestFrame();
            metrics->setText(QStringLiteral("本次实际收到：%1 帧    |    画面尺寸：%2 × %3")
                                 .arg(frameCount).arg(lastFrameSize.width()).arg(lastFrameSize.height()));
        });

    QObject::connect(&frameWatchdog, &QTimer::timeout, &window, [&] {
        if (captureRequested && frameClock.isValid() && frameClock.elapsed() >= 8000) {
            const QString message = receivedFrameThisRun
                ? QStringLiteral("已连续 8 秒没有收到视频帧，预览已停止。请检查设备连接。")
                : QStringLiteral("8 秒内未收到有效视频帧，已停止。请检查 USB 透传、设备占用和驱动。 ");
            errors << "NO_FRAMES " << message.trimmed() << '\n';
            errors.flush();
            stopCapture(message.trimmed());
        }
    });

    QObject::connect(&camera, &QCamera::errorOccurred, &window,
        [&](QCamera::Error error, const QString& message) {
            if (error == QCamera::NoError)
                return;
            const QString details = message.isEmpty() ? QStringLiteral("未知摄像头错误") : message;
            errors << "CAMERA_ERROR " << details << '\n';
            errors.flush();
            stopCapture(QStringLiteral("摄像头错误：%1").arg(details));
        });
    QObject::connect(&camera, &QCamera::activeChanged, &window, [&](bool active) {
        if (active && captureRequested && !receivedFrameThisRun)
            status->setText(QStringLiteral("摄像头已启动，正在等待实际视频帧…"));
        else if (!active && captureRequested)
            stopCapture(QStringLiteral("摄像头已停止输出，旧画面已清空。"));
    });
    QObject::connect(&mediaDevices, &QMediaDevices::videoInputsChanged, &window, [&] {
        if (!captureRequested) {
            refreshDevices(true);
            return;
        }
        const QByteArray currentId = camera.cameraDevice().id();
        const auto availableDevices = QMediaDevices::videoInputs();
        const bool stillPresent = std::any_of(availableDevices.cbegin(), availableDevices.cend(),
            [&](const QCameraDevice& device) { return device.id() == currentId; });
        if (!stillPresent)
            stopCapture(QStringLiteral("正在使用的摄像头已断开，预览已停止。请重新连接后刷新。"));
        // A newly plugged device never changes the running camera. The device
        // list is rebuilt after stop, when changing selection is safe.
    });

    refreshDevices(true);
    window.show();
    auto fitVideoArea = [&] {
        const int availableWidth = std::max(320, window.width() - 48);
        const int statusHeight = status->hasHeightForWidth()
            ? status->heightForWidth(availableWidth) : status->sizeHint().height();
        const int reservedHeight = title->sizeHint().height() + subtitle->sizeHint().height()
            + controls->sizeHint().height() + metrics->sizeHint().height()
            + std::max(statusHeight, status->minimumSizeHint().height())
            + 5 * layout->spacing() + 36 + 2;
        const int availableHeight = std::max(180, window.height() - reservedHeight);
        const int videoWidth = std::min(availableWidth, availableHeight * 16 / 9);
        pages->setFixedSize(videoWidth, videoWidth * 9 / 16);
        if (captureRequested)
            displayLatestFrame();
    };
    QObject::connect(window.windowHandle(), &QWindow::widthChanged, &window,
        [&](int) { fitVideoArea(); });
    QObject::connect(window.windowHandle(), &QWindow::heightChanged, &window,
        [&](int) { fitVideoArea(); });
    fitVideoArea();

    if (parser.isSet(QStringLiteral("auto-start")))
        QTimer::singleShot(0, &window, startCapture);
    if (quitAfterMs > 0)
        QTimer::singleShot(quitAfterMs, &app, &QCoreApplication::quit);
    if (!snapshotPath.isEmpty()) {
        QTimer::singleShot(500, &window, [&, snapshotPath] {
            // Never save a real camera frame, even when preview is stopped.
            if (!QMediaDevices::videoInputs().isEmpty() || everReceivedFrame || captureRequested) {
                errors << "SNAPSHOT_SKIPPED 检测到摄像头或已接收视频帧，未保存截图。\n";
                errors.flush();
                return;
            }
            const QFileInfo destination(snapshotPath);
            if (!QDir().mkpath(destination.absolutePath()) || !window.grab().save(snapshotPath)) {
                errors << "SNAPSHOT_ERROR 无法保存空状态截图。\n";
                errors.flush();
                return;
            }
            output << "SNAPSHOT " << destination.absoluteFilePath() << '\n';
            output.flush();
        });
    }

    const int result = app.exec();

    // Explicitly detach the capture chain before stack objects and UI state
    // disappear; disconnect callbacks before stopping during final teardown.
    QObject::disconnect(&camera, nullptr, &window, nullptr);
    QObject::disconnect(&videoSink, nullptr, &window, nullptr);
    QObject::disconnect(&mediaDevices, nullptr, &window, nullptr);
    frameWatchdog.stop();
    camera.stop();
    session.setVideoSink(nullptr);
    session.setCamera(nullptr);
    output << "FRAMES " << frameCount << '\n';
    output.flush();
    return result;
}
