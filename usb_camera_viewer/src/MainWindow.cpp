#include "MainWindow.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , controller_(this)
{
    buildUi();

    // 控制器信号 -> 界面更新。全部在主线程（GUI 线程）执行
    QObject::connect(&controller_, &CameraController::frameReady, this, [this](const QImage &image) {
        // QImage 是隐式共享类型，这里只是共享同一份像素，不产生深拷贝
        latestFrame_ = image;
        lastFrameSize_ = image.size();
        if (waitingFirstFrame_) {
            waitingFirstFrame_ = false;
            pages_->setCurrentIndex(1);
            status_->setText(QStringLiteral("正在预览 USB 摄像头，已收到真实视频帧。"));
        }
        displayLatestFrame();
        metrics_->setText(QStringLiteral("本次实际收到：%1 帧    |    画面尺寸：%2 × %3")
                              .arg(controller_.frameCount())
                              .arg(lastFrameSize_.width())
                              .arg(lastFrameSize_.height()));
    });

    QObject::connect(&controller_, &CameraController::stopped, this, [this](const QString &message) {
        latestFrame_ = QImage();
        videoImage_->clear();
        pages_->setCurrentIndex(0);
        refreshDevices(false);
        placeholder_->setText(message);
        status_->setText(message);
    });

    QObject::connect(&controller_, &CameraController::waitingForFirstFrame, this, [this] {
        status_->setText(QStringLiteral("摄像头已启动，正在等待实际视频帧…"));
    });

    QObject::connect(&controller_, &CameraController::deviceListChanged, this, [this] {
        refreshDevices(true);
    });

    refreshDevices(true);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("USB 摄像头预览 · C++ / Qt"));
    resize(1000, 700);
    setMinimumSize(760, 560);
    setStyleSheet(QStringLiteral(R"(
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

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(24, 18, 24, 18);
    layout_->setSpacing(10);

    title_ = new QLabel(QStringLiteral("USB 摄像头预览"));
    title_->setObjectName(QStringLiteral("title"));
    layout_->addWidget(title_);

    subtitle_ = new QLabel(QStringLiteral("选择设备，查看实时摄像头画面。"));
    subtitle_->setObjectName(QStringLiteral("subtitle"));
    layout_->addWidget(subtitle_);

    auto *controls = new QHBoxLayout;
    controls_ = controls;
    deviceBox_ = new QComboBox;
    refreshButton_ = new QPushButton(QStringLiteral("刷新"));
    openButton_ = new QPushButton(QStringLiteral("打开"));
    stopButton_ = new QPushButton(QStringLiteral("停止"));
    openButton_->setObjectName(QStringLiteral("open"));
    controls->addWidget(new QLabel(QStringLiteral("设备")));
    controls->addWidget(deviceBox_, 1);
    controls->addWidget(refreshButton_);
    controls->addWidget(openButton_);
    controls->addWidget(stopButton_);
    layout_->addLayout(controls);

    // 画面区域固定 16:9，普通 QLabel 可避免在 WSLg 下出现原生视频子窗口
    pages_ = new QStackedWidget;
    placeholder_ = new QLabel;
    placeholder_->setObjectName(QStringLiteral("placeholder"));
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    videoImage_ = new QLabel;
    videoImage_->setObjectName(QStringLiteral("videoImage"));
    videoImage_->setAlignment(Qt::AlignCenter);
    videoImage_->setMinimumSize(1, 1);
    videoImage_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    pages_->addWidget(placeholder_);
    pages_->addWidget(videoImage_);
    pages_->setCurrentIndex(0);
    layout_->addWidget(pages_, 1, Qt::AlignCenter);

    metrics_ = new QLabel(QStringLiteral("本次实际收到：0 帧    |    画面尺寸：—"));
    metrics_->setObjectName(QStringLiteral("metrics"));
    layout_->addWidget(metrics_);

    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("status"));
    status_->setWordWrap(true);
    layout_->addWidget(status_);

    QObject::connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshDevices(true); });
    QObject::connect(openButton_, &QPushButton::clicked, this, [this] { startCapture(); });
    QObject::connect(stopButton_, &QPushButton::clicked, this,
        [this] { stopCapture(QStringLiteral("预览已停止，旧画面已清空。")); });
}

void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // windowHandle() 只有在窗口显示后才有效，所以首次显示时再监听尺寸变化
    if (!resizeWatched_ && windowHandle()) {
        resizeWatched_ = true;
        QObject::connect(windowHandle(), &QWindow::widthChanged, this, [this](int) { fitVideoArea(); });
        QObject::connect(windowHandle(), &QWindow::heightChanged, this, [this](int) { fitVideoArea(); });
        fitVideoArea();
    }
}

void MainWindow::startCapture()
{
    if (controller_.isCapturing())
        return;

    const QByteArray selectedId = deviceBox_->currentData().toByteArray();
    if (!controller_.start(selectedId)) {
        refreshDevices(true);
        status_->setText(QStringLiteral("所选摄像头不可用，请检查 USB 连接后刷新。"));
        return;
    }

    latestFrame_ = QImage();
    videoImage_->clear();
    waitingFirstFrame_ = true;
    metrics_->setText(QStringLiteral("本次实际收到：0 帧    |    画面尺寸：—"));
    placeholder_->setText(QStringLiteral("正在打开摄像头…\n\n等待第一帧，最长 8 秒。"));
    pages_->setCurrentIndex(0);
    status_->setText(QStringLiteral("正在启动摄像头，等待实际视频帧…"));
    deviceBox_->setEnabled(false);
    refreshButton_->setEnabled(false);
    openButton_->setEnabled(false);
    stopButton_->setEnabled(true);
}

void MainWindow::stopCapture(const QString &message)
{
    controller_.stop(message);
}

void MainWindow::refreshDevices(bool updateStatus)
{
    if (controller_.isCapturing())
        return;

    const QByteArray previousId = deviceBox_->currentData().toByteArray();
    cameraDevices_ = QMediaDevices::videoInputs();
    const QSignalBlocker blocker(deviceBox_);
    deviceBox_->clear();

    int selectedIndex = -1;
    int defaultIndex = 0;
    for (qsizetype index = 0; index < cameraDevices_.size(); ++index) {
        const QCameraDevice &device = cameraDevices_[index];
        deviceBox_->addItem(device.description(), device.id());
        if (device.id() == previousId)
            selectedIndex = static_cast<int>(index);
        if (device.isDefault())
            defaultIndex = static_cast<int>(index);
    }

    if (cameraDevices_.isEmpty()) {
        deviceBox_->addItem(QStringLiteral("没有可用摄像头"));
        placeholder_->setText(QStringLiteral("WSL 尚未识别 USB 摄像头\n\n请先检查 USB 透传、UVC 驱动和 /dev/video*，然后点击刷新。"));
        if (updateStatus)
            status_->setText(QStringLiteral("未发现视频输入设备。尚未打开摄像头，也未收到视频帧。"));
    } else {
        deviceBox_->setCurrentIndex(selectedIndex >= 0 ? selectedIndex : defaultIndex);
        placeholder_->setText(QStringLiteral("摄像头就绪\n\n选择设备，点击“打开”开始预览。"));
        if (updateStatus)
            status_->setText(QStringLiteral("已发现 %1 个摄像头；等待打开。").arg(cameraDevices_.size()));
    }

    deviceBox_->setEnabled(!cameraDevices_.isEmpty());
    refreshButton_->setEnabled(true);
    openButton_->setEnabled(!cameraDevices_.isEmpty());
    stopButton_->setEnabled(false);
}

void MainWindow::displayLatestFrame()
{
    if (latestFrame_.isNull())
        return;
    videoImage_->setPixmap(QPixmap::fromImage(latestFrame_).scaled(
        pages_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::fitVideoArea()
{
    const int availableWidth = std::max(320, width() - 48);
    const int statusHeight = status_->hasHeightForWidth()
        ? status_->heightForWidth(availableWidth) : status_->sizeHint().height();
    const int reservedHeight = title_->sizeHint().height() + subtitle_->sizeHint().height()
        + controls_->sizeHint().height() + metrics_->sizeHint().height()
        + 5 * layout_->spacing() + 36 + 2
        + std::max(statusHeight, status_->minimumSizeHint().height());
    const int availableHeight = std::max(180, height() - reservedHeight);
    const int videoWidth = std::min(availableWidth, availableHeight * 16 / 9);
    pages_->setFixedSize(videoWidth, videoWidth * 9 / 16);
    if (controller_.isCapturing())
        displayLatestFrame();
}
