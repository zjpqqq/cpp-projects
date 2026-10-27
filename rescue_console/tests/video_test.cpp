#include "videoworker.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

bool nearChannel(int actual, int expected)
{
    // MJPG is lossy, so verify channel order with a generous codec tolerance.
    return std::abs(actual - expected) <= 18;
}

void stopOrExit(VideoWorker &worker)
{
    worker.requestStop();
    if (!worker.wait(2000)) {
        std::cerr << "FAIL: VideoWorker did not stop within 2 seconds\n";
        std::cerr.flush();
        // Never destroy a still-running QThread or leave this test hanging.
        std::_Exit(EXIT_FAILURE);
    }
}

bool testDecodedFramesAndStop(const QString &path)
{
    VideoWorker worker(path);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    int frames = 0;
    bool pixelsOk = true;
    bool requestedStop = false;
    bool timedOut = false;
    QElapsedTimer stopTime;
    qint64 stopElapsed = -1;

    QObject::connect(&worker, &VideoWorker::frameReady, &loop,
                     [&](const QImage &image) {
        ++frames;
        pixelsOk = pixelsOk && image.size() == QSize(160, 96);
        if (!image.isNull()) {
            const QColor pixel = image.pixelColor(image.width() / 2,
                                                  image.height() / 2);
            // The fixture was written in BGR=(20,90,210).
            pixelsOk = pixelsOk && nearChannel(pixel.red(), 210)
                      && nearChannel(pixel.green(), 90)
                      && nearChannel(pixel.blue(), 20);
        } else {
            pixelsOk = false;
        }
        if (frames >= 3 && !requestedStop) {
            requestedStop = true;
            stopTime.start();
            worker.requestStop();
        }
    });
    QObject::connect(&worker, &QThread::finished, &loop, [&]() {
        if (requestedStop)
            stopElapsed = stopTime.elapsed();
        loop.quit();
    });
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    deadline.start(6000);
    worker.start();
    loop.exec();
    stopOrExit(worker);

    if (timedOut || frames < 3 || !pixelsOk || stopElapsed < 0
        || stopElapsed > 2000) {
        std::cerr << "FAIL: local video decoding/stop: frames=" << frames
                  << " pixels_ok=" << pixelsOk
                  << " stop_ms=" << stopElapsed
                  << " timed_out=" << timedOut << '\n';
        return false;
    }
    std::cout << "PASS: decoded " << frames
              << " local MJPG frames; RGB and dimensions correct; stop_ms="
              << stopElapsed << '\n';
    return true;
}

bool testMissingFile(const QString &path)
{
    VideoWorker worker(path);
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    bool timedOut = false;
    int frames = 0;
    int statuses = 0;

    QObject::connect(&worker, &VideoWorker::frameReady, &loop,
                     [&](const QImage &) { ++frames; });
    QObject::connect(&worker, &VideoWorker::statusChanged, &loop,
                     [&](const QString &) { ++statuses; });
    QObject::connect(&worker, &QThread::finished, &loop, &QEventLoop::quit);
    QObject::connect(&deadline, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    deadline.start(6000);
    worker.start();
    loop.exec();
    stopOrExit(worker);

    if (timedOut || frames != 0 || statuses == 0) {
        std::cerr << "FAIL: missing local video: frames=" << frames
                  << " statuses=" << statuses
                  << " timed_out=" << timedOut << '\n';
        return false;
    }
    std::cout << "PASS: missing local video exits without emitting frames\n";
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "FAIL: cannot create temporary fixture directory\n";
        return EXIT_FAILURE;
    }

    const QString path = temporary.filePath("local_video.avi");
    cv::VideoWriter writer(path.toStdString(),
                           cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                           25.0, cv::Size(160, 96));
    if (!writer.isOpened()) {
        std::cerr << "FAIL: OpenCV cannot encode the MJPG test fixture\n";
        return EXIT_FAILURE;
    }
    for (int frame = 0; frame < 50; ++frame)
        writer.write(cv::Mat(96, 160, CV_8UC3, cv::Scalar(20, 90, 210)));
    writer.release();

    const bool decoded = testDecodedFramesAndStop(path);
    const bool missing = testMissingFile(temporary.filePath("missing_video.avi"));
    return decoded && missing ? EXIT_SUCCESS : EXIT_FAILURE;
}
