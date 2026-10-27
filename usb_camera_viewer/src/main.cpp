// 程序入口：只负责命令行参数与启动，界面和采集逻辑在 MainWindow / CameraController 中

#include <QApplication>
#include <QCameraDevice>
#include <QCameraFormat>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QMediaDevices>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>
#include <QVideoFrameFormat>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("usb_camera_viewer"));
    QApplication::setApplicationVersion(QStringLiteral("0.2.0"));

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
                for (const QCameraFormat &format : cameras[index].videoFormats()) {
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

    MainWindow window;
    window.show();

    if (parser.isSet(QStringLiteral("auto-start")))
        QTimer::singleShot(0, &window, [&window] { window.startCapture(); });
    if (quitAfterMs > 0)
        QTimer::singleShot(quitAfterMs, &app, &QCoreApplication::quit);
    if (!snapshotPath.isEmpty()) {
        QTimer::singleShot(500, &window, [&window, &output, &errors, snapshotPath] {
            // 有摄像头或收到过帧时绝不保存真实画面
            if (!QMediaDevices::videoInputs().isEmpty() || window.hasEverReceivedFrame()
                || window.isCapturing()) {
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

    output << "FRAMES " << window.frameCount() << '\n';
    output.flush();
    return result;
}
