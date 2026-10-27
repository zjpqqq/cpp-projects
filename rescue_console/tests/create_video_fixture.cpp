#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "Usage: create_video_fixture output.avi\n";
        return EXIT_FAILURE;
    }
    try {
        const std::filesystem::path output(argv[1]);
        if (!output.parent_path().empty())
            std::filesystem::create_directories(output.parent_path());
        cv::VideoWriter writer(output.string(),
                               cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
                               25.0, cv::Size(960, 540));
        if (!writer.isOpened()) {
            std::cerr << "Cannot create MJPG fixture: " << output << '\n';
            return EXIT_FAILURE;
        }

        constexpr int count = 150;
        for (int index = 0; index < count; ++index) {
            cv::Mat image(540, 960, CV_8UC3, cv::Scalar(41, 32, 24));
            cv::rectangle(image, cv::Rect(0, 260, 960, 280),
                          cv::Scalar(48, 48, 44), cv::FILLED);
            cv::rectangle(image, cv::Rect(80, 130, 190, 170),
                          cv::Scalar(72, 69, 62), cv::FILLED);
            cv::rectangle(image, cv::Rect(690, 100, 170, 200),
                          cv::Scalar(65, 63, 56), cv::FILLED);
            for (int row = 300; row < 540; row += 40)
                cv::line(image, {0, row}, {960, row}, cv::Scalar(64, 62, 52), 1);
            for (int x = 0; x < 960; x += 120)
                cv::line(image, {480, 260}, {x, 540}, cv::Scalar(64, 62, 52), 1);

            const double phase = 2.0 * CV_PI * index / count;
            const int x = 465 + static_cast<int>(105.0 * std::sin(phase));
            const int y = 307;
            const cv::Scalar orange(45, 157, 243);
            cv::circle(image, {x, y - 64}, 18, orange, cv::FILLED, cv::LINE_AA);
            cv::line(image, {x, y - 44}, {x, y + 23}, orange, 23, cv::LINE_AA);
            cv::line(image, {x, y - 26}, {x - 30, y + 5}, orange, 11, cv::LINE_AA);
            cv::line(image, {x, y - 26}, {x + 30, y + 5}, orange, 11, cv::LINE_AA);
            cv::line(image, {x, y + 18}, {x - 22, y + 63}, orange, 12, cv::LINE_AA);
            cv::line(image, {x, y + 18}, {x + 22, y + 63}, orange, 12, cv::LINE_AA);

            cv::rectangle(image, {0, 0}, {960, 78}, cv::Scalar(28, 23, 19),
                          cv::FILLED);
            cv::putText(image, "SIMULATION - LOCAL VIDEO FIXTURE", {28, 34},
                        cv::FONT_HERSHEY_SIMPLEX, 0.76,
                        cv::Scalar(110, 217, 251), 2, cv::LINE_AA);
            cv::putText(image, "Synthetic scene / No model inference", {28, 61},
                        cv::FONT_HERSHEY_SIMPLEX, 0.5,
                        cv::Scalar(170, 170, 166), 1, cv::LINE_AA);
            cv::putText(image, "FRAME " + std::to_string(index), {28, 510},
                        cv::FONT_HERSHEY_SIMPLEX, 0.55,
                        cv::Scalar(195, 195, 186), 1, cv::LINE_AA);
            writer.write(image);
        }
        writer.release();
        std::cout << "Created " << output.string() << " (960x540, 25 fps, "
                  << count << " synthetic frames)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << "Fixture generation failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
