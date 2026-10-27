// 主窗口
// 只负责界面：布局、设备选择、画面显示、状态提示。
// 采集相关的逻辑全部在 CameraController 中，本类通过信号接收结果。

#ifndef USB_CAMERA_VIEWER_MAIN_WINDOW_H
#define USB_CAMERA_VIEWER_MAIN_WINDOW_H

#include <QCameraDevice>
#include <QImage>
#include <QSize>
#include <QWidget>

#include <QList>

#include "CameraController.h"

class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QVBoxLayout;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // 供命令行选项调用
    void startCapture();
    void stopCapture(const QString &message);
    bool isCapturing() const { return controller_.isCapturing(); }
    bool hasEverReceivedFrame() const { return controller_.hasEverReceivedFrame(); }
    quint64 frameCount() const { return controller_.frameCount(); }

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void refreshDevices(bool updateStatus);
    void displayLatestFrame();
    void fitVideoArea();

    // 采集控制器：作为成员对象随窗口一起析构
    CameraController controller_;

    QLabel *title_ = nullptr;
    QLabel *subtitle_ = nullptr;
    QHBoxLayout *controls_ = nullptr;
    QComboBox *deviceBox_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *stopButton_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QLabel *placeholder_ = nullptr;
    QLabel *videoImage_ = nullptr;
    QLabel *metrics_ = nullptr;
    QLabel *status_ = nullptr;
    QVBoxLayout *layout_ = nullptr;

    QList<QCameraDevice> cameraDevices_;
    QImage latestFrame_;      // 最近一帧，窗口缩放时用它重绘
    QSize lastFrameSize_;
    bool waitingFirstFrame_ = false;
    bool resizeWatched_ = false;
};

#endif // USB_CAMERA_VIEWER_MAIN_WINDOW_H
