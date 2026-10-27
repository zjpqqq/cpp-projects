#include "videoworker.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

VideoWorker::VideoWorker(QString source, QObject* parent)
    : QThread(parent), m_source(std::move(source))
{
}

void VideoWorker::requestStop()
{
    requestInterruption();
}

bool VideoWorker::sleepUntil(qint64 deadlineMs, const QElapsedTimer& clock)
{
    while (!isInterruptionRequested()) {
        const qint64 remaining = deadlineMs - clock.elapsed();
        if (remaining <= 0)
            return true;
        QThread::msleep(static_cast<unsigned long>(std::min<qint64>(remaining, 20)));
    }
    return false;
}

void VideoWorker::run()
{
    const QString source = m_source.trimmed();
    if (source.isEmpty()) {
        emit statusChanged(QStringLiteral("视频源为空"));
        return;
    }

    const bool cameraSource = QRegularExpression(QStringLiteral("^\\d+$")).match(source).hasMatch();
    const QUrl sourceUrl(source);
    const QString captureSource = sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : source;
    const bool fileSource = !cameraSource && QFileInfo::exists(captureSource)
        && QFileInfo(captureSource).isFile();
    const bool networkSource = !cameraSource && !fileSource && sourceUrl.isValid()
        && QStringList{QStringLiteral("rtsp"), QStringLiteral("rtsps"), QStringLiteral("http"),
               QStringLiteral("https"), QStringLiteral("rtmp"), QStringLiteral("udp"),
               QStringLiteral("tcp")}.contains(sourceUrl.scheme().toLower());

    if (!cameraSource && !fileSource && !networkSource) {
        emit statusChanged(QStringLiteral("找不到视频文件，或视频地址协议不受支持"));
        return;
    }

    // VideoCapture is constructed, used and released in this one worker thread.
    cv::VideoCapture capture;
    try {
        emit statusChanged(QStringLiteral("正在打开视频源…"));
        bool opened = false;
        if (cameraSource) {
            bool validIndex = false;
            const int index = source.toInt(&validIndex);
            if (validIndex)
                opened = capture.open(index, cv::CAP_ANY);
        } else {
            // These are open-only properties in OpenCV's FFmpeg backend.
            // Do not fall back to another backend for network streams, because
            // that would silently discard the requested timeout behavior.
            const std::vector<int> parameters{
                cv::CAP_PROP_OPEN_TIMEOUT_MSEC, 3000,
                cv::CAP_PROP_READ_TIMEOUT_MSEC, 3000,
            };
            opened = capture.open(captureSource.toStdString(), cv::CAP_FFMPEG, parameters);
            if (!opened && fileSource && !isInterruptionRequested())
                opened = capture.open(captureSource.toStdString(), cv::CAP_ANY);
        }

        if (isInterruptionRequested()) {
            capture.release();
            emit statusChanged(QStringLiteral("视频采集已停止"));
            return;
        }
        if (!opened) {
            emit statusChanged(cameraSource
                    ? QStringLiteral("摄像头打开失败；WSL 摄像头需要单独配置设备透传与驱动")
                    : QStringLiteral("视频源打开失败，请检查文件、地址和解码支持"));
            return;
        }

        double fps = capture.get(cv::CAP_PROP_FPS);
        if (!std::isfinite(fps) || fps < 1.0 || fps > 240.0)
            fps = 25.0;
        emit streamInfo(QStringLiteral("%1 × %2 · 源帧率 %3 FPS")
                            .arg(static_cast<int>(capture.get(cv::CAP_PROP_FRAME_WIDTH)))
                            .arg(static_cast<int>(capture.get(cv::CAP_PROP_FRAME_HEIGHT)))
                            .arg(fps, 0, 'f', 1));
        emit statusChanged(fileSource ? QStringLiteral("本地视频播放中（自动循环）")
                                      : QStringLiteral("视频采集中"));

        QElapsedTimer clock;
        clock.start();
        double nextFrameMs = 0.0;
        qint64 lastEmissionMs = -1000;
        bool firstFrame = true;
        while (!isInterruptionRequested()) {
            if (fileSource && !sleepUntil(static_cast<qint64>(nextFrameMs), clock))
                break;

            cv::Mat frame;
            if (!capture.read(frame) || frame.empty()) {
                if (isInterruptionRequested())
                    break;
                const double frameCount = capture.get(cv::CAP_PROP_FRAME_COUNT);
                const double position = capture.get(cv::CAP_PROP_POS_FRAMES);
                const bool endOfFile = fileSource && frameCount > 0.0
                    && position >= frameCount - 1.0;
                if (endOfFile && capture.set(cv::CAP_PROP_POS_FRAMES, 0.0)
                    && capture.read(frame) && !frame.empty()) {
                    nextFrameMs = clock.elapsed();
                } else {
                    emit statusChanged(fileSource ? QStringLiteral("视频读取失败或无法循环此文件")
                                                  : QStringLiteral("视频读取中断或超时，采集已停止"));
                    break;
                }
            }

            // Keep file playback at its source rate; render at most ~30 FPS.
            if (fileSource) {
                nextFrameMs += 1000.0 / fps;
                if (nextFrameMs < static_cast<double>(clock.elapsed()) - 200.0)
                    nextFrameMs = clock.elapsed();
            }
            const qint64 now = clock.elapsed();
            if (!firstFrame && now - lastEmissionMs < 32)
                continue;

            cv::Mat pixels;
            if (frame.depth() != CV_8U)
                frame.convertTo(frame, CV_8U);
            QImage::Format format;
            if (frame.channels() == 3) {
                cv::cvtColor(frame, pixels, cv::COLOR_BGR2RGB);
                format = QImage::Format_RGB888;
            } else if (frame.channels() == 4) {
                cv::cvtColor(frame, pixels, cv::COLOR_BGRA2RGBA);
                format = QImage::Format_RGBA8888;
            } else if (frame.channels() == 1) {
                pixels = frame;
                format = QImage::Format_Grayscale8;
            } else {
                emit statusChanged(QStringLiteral("不支持此视频帧的通道格式"));
                break;
            }

            // copy() detaches from cv::Mat before the queued signal crosses
            // threads. The next read cannot overwrite an image being painted.
            QImage image(pixels.data, pixels.cols, pixels.rows,
                static_cast<qsizetype>(pixels.step), format);
            emit frameReady(image.copy());
            firstFrame = false;
            lastEmissionMs = now;
        }
        capture.release();
        if (isInterruptionRequested())
            emit statusChanged(QStringLiteral("视频采集已停止"));
    } catch (const cv::Exception&) {
        // Avoid logging source URLs, which may contain camera credentials.
        emit statusChanged(QStringLiteral("OpenCV 视频解码异常，采集已停止"));
    } catch (const std::exception&) {
        emit statusChanged(QStringLiteral("视频采集异常，采集已停止"));
    }
}
