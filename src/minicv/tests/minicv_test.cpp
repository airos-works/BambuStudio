// Self-test of minicv against the values OpenCV 4 gives for the same calls.
// Built as the target minicv_test; prints one line per check and exits with
// the number of failures.
#include <cstdio>
#include <cstdlib>
#include <set>

#include "opencv2/opencv.hpp"
#include "test_images.h"

static int failures = 0;

static void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        ++failures;
}

static bool near(const cv::Vec3b &a, int v0, int v1, int v2, int tolerance = 1)
{
    return std::abs(a[0] - v0) <= tolerance && std::abs(a[1] - v1) <= tolerance && std::abs(a[2] - v2) <= tolerance;
}

int main()
{
    // --- colour conversions: OpenCV's 8-bit results
    {
        cv::Mat rgb(4, 1, CV_8UC3), out, back;
        rgb.at<cv::Vec3b>(0, 0) = cv::Vec3b(255, 0, 0);
        rgb.at<cv::Vec3b>(1, 0) = cv::Vec3b(0, 255, 0);
        rgb.at<cv::Vec3b>(2, 0) = cv::Vec3b(0, 0, 255);
        rgb.at<cv::Vec3b>(3, 0) = cv::Vec3b(255, 255, 255);
        cv::cvtColor(rgb, out, cv::COLOR_RGB2Lab);
        check(near(out.at<cv::Vec3b>(0, 0), 136, 208, 195), "RGB2Lab red = (136, 208, 195)");
        check(near(out.at<cv::Vec3b>(1, 0), 224, 42, 211), "RGB2Lab green = (224, 42, 211)");
        check(near(out.at<cv::Vec3b>(2, 0), 82, 207, 20), "RGB2Lab blue = (82, 207, 20)");
        check(near(out.at<cv::Vec3b>(3, 0), 255, 128, 128), "RGB2Lab white = (255, 128, 128)");
        cv::cvtColor(out, back, cv::COLOR_Lab2RGB);
        // 8-bit a and b lose precision that the sRGB curve amplifies near black
        bool round_trip = true;
        for (int i = 0; i < 4; ++i) {
            const cv::Vec3b a = rgb.at<cv::Vec3b>(i, 0), b = back.at<cv::Vec3b>(i, 0);
            if (!near(b, a[0], a[1], a[2], 8)) {
                std::printf("      (%d, %d, %d) came back as (%d, %d, %d)\n", a[0], a[1], a[2], b[0], b[1], b[2]);
                round_trip = false;
            }
        }
        check(round_trip, "Lab2RGB(RGB2Lab(x)) = x within 8");
        cv::cvtColor(rgb, out, cv::COLOR_RGB2HSV);
        check(near(out.at<cv::Vec3b>(0, 0), 0, 255, 255, 0), "RGB2HSV red = (0, 255, 255)");
        check(near(out.at<cv::Vec3b>(1, 0), 60, 255, 255, 0), "RGB2HSV green = (60, 255, 255)");
        check(near(out.at<cv::Vec3b>(2, 0), 120, 255, 255, 0), "RGB2HSV blue = (120, 255, 255)");
        check(near(out.at<cv::Vec3b>(3, 0), 0, 0, 255, 0), "RGB2HSV white = (0, 0, 255)");
        cv::cvtColor(out, out, cv::COLOR_HSV2RGB); // in place
        check(near(out.at<cv::Vec3b>(1, 0), 0, 255, 0, 0) && near(out.at<cv::Vec3b>(3, 0), 255, 255, 255, 0), "HSV2RGB in place");
        cv::Mat rgba(1, 1, CV_8UC4), bgr;
        rgba.at<cv::Vec4b>(0, 0) = cv::Vec4b(10, 20, 30, 40);
        cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
        check(bgr.channels() == 3 && near(bgr.at<cv::Vec3b>(0, 0), 30, 20, 10, 0), "RGBA2BGR");
    }

    // --- the random generator: OpenCV's sequence for the seed 12345
    {
        cv::RNG rng(12345);
        const unsigned a = rng.next(), b = rng.next();
        // state = low32(state) * 4164903690 + (state >> 32), the result is its low 32 bits
        check(a == 682552634u && b == 3453542663u, "RNG(12345) sequence");
    }

    // --- k-means: two tight groups of colours
    {
        const int   n = 200;
        cv::Mat     samples(n, 1, CV_32FC3), labels, centers;
        for (int i = 0; i < n; ++i) {
            const float jitter = static_cast<float>(i % 5);
            samples.at<cv::Vec3f>(i, 0) = i % 2 ? cv::Vec3f(200 + jitter, 30, 40) : cv::Vec3f(20, 120 + jitter, 220);
        }
        cv::theRNG().state = 12345;
        const double compactness = cv::kmeans(samples, 2, labels, cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 300, 0.5), 3,
                                              cv::KMEANS_PP_CENTERS, centers);
        bool split = labels.rows == n && centers.rows == 2 && centers.cols == 3 && centers.type() == CV_32FC1;
        for (int i = 2; split && i < n; ++i)
            split = labels.at<int>(i, 0) == labels.at<int>(i % 2, 0);
        split = split && labels.at<int>(0, 0) != labels.at<int>(1, 0);
        check(split, "kmeans separates two groups");
        const cv::Vec3f odd = centers.at<cv::Vec3f>(labels.at<int>(1, 0));
        check(std::abs(odd[0] - 202.f) < 0.5f && std::abs(odd[1] - 30.f) < 0.01f && std::abs(odd[2] - 40.f) < 0.01f, "kmeans centers are the group means");
        check(compactness > 0. && compactness < n * 4.1, "kmeans compactness");
        // as many clusters as distinct samples
        cv::Mat three(3, 1, CV_32FC3);
        three.at<cv::Vec3f>(0, 0) = cv::Vec3f(0, 0, 0);
        three.at<cv::Vec3f>(1, 0) = cv::Vec3f(100, 0, 0);
        three.at<cv::Vec3f>(2, 0) = cv::Vec3f(0, 100, 0);
        cv::kmeans(three, 3, labels, cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 300, 0.5), 3, cv::KMEANS_PP_CENTERS, centers);
        std::set<int> distinct;
        for (int i = 0; i < 3; ++i)
            distinct.insert(labels.at<int>(i, 0));
        check(distinct.size() == 3, "kmeans with K = number of samples");
    }

    // --- Mat: views, copies, conversion
    {
        cv::Mat atlas = cv::Mat::zeros(4, 4, CV_8UC3), patch(2, 2, CV_8UC3);
        patch.setTo(cv::Scalar(1, 2, 3));
        cv::Mat roi = atlas(cv::Rect(1, 2, 2, 2));
        patch.copyTo(roi);
        check(near(atlas.at<cv::Vec3b>(2, 1), 1, 2, 3, 0) && near(atlas.at<cv::Vec3b>(3, 2), 1, 2, 3, 0) && near(atlas.at<cv::Vec3b>(1, 1), 0, 0, 0, 0),
              "copyTo into a view writes the parent");
        check(!roi.isContinuous() && atlas.isContinuous() && atlas.step[0] == 12, "isContinuous and step");
        cv::Mat f;
        atlas.convertTo(f, CV_32FC3);
        check(f.type() == CV_32FC3 && f.at<cv::Vec3f>(2, 1)[2] == 3.f, "convertTo 32F");
        cv::Mat copy = atlas.clone();
        copy.at<cv::Vec3b>(2, 1) = cv::Vec3b(9, 9, 9);
        check(near(atlas.at<cv::Vec3b>(2, 1), 1, 2, 3, 0), "clone is a deep copy");
        const cv::Vec3b from_float = cv::Vec3f(-5.f, 99.6f, 300.f);
        check(near(from_float, 0, 100, 255, 0), "Vec3f to Vec3b saturates and rounds");
    }

    // --- decoding
    {
        const cv::Mat png_bytes(1, static_cast<int>(sizeof(kTestPng)), CV_8UC1, const_cast<unsigned char *>(kTestPng));
        cv::Mat       image = cv::imdecode(png_bytes, cv::IMREAD_COLOR);
        check(image.rows == 2 && image.cols == 2 && image.channels() == 3, "imdecode PNG as colour: 2x2, 3 channels");
        check(!image.empty() && near(image.at<cv::Vec3b>(0, 0), 0, 0, 255, 0) && near(image.at<cv::Vec3b>(0, 1), 0, 255, 0, 0) &&
                  near(image.at<cv::Vec3b>(1, 0), 255, 0, 0, 0),
              "imdecode PNG pixels are BGR");
        image = cv::imdecode(png_bytes, cv::IMREAD_UNCHANGED);
        check(image.channels() == 4 && image.at<cv::Vec4b>(1, 1)[3] == 0 && image.at<cv::Vec4b>(0, 0)[2] == 255, "imdecode PNG unchanged keeps alpha");
        const cv::Mat jpeg_bytes(1, static_cast<int>(sizeof(kTestJpeg)), CV_8UC1, const_cast<unsigned char *>(kTestJpeg));
        image = cv::imdecode(jpeg_bytes, cv::IMREAD_COLOR);
        check(image.rows == 16 && image.cols == 16 && image.channels() == 3 && near(image.at<cv::Vec3b>(8, 8), 50, 100, 200, 3),
              "imdecode JPEG: 16x16 of BGR (50, 100, 200)");
        const cv::Mat garbage(1, 4, CV_8UC1, const_cast<unsigned char *>(kTestPng) + 20);
        check(cv::imdecode(garbage, cv::IMREAD_COLOR).empty(), "imdecode of garbage is empty");
    }

    // --- contours: a 6x6 square with a 2x2 hole, and a lone pixel
    {
        cv::Mat image = cv::Mat::zeros(12, 12, CV_8UC1);
        for (int y = 2; y < 8; ++y)
            for (int x = 2; x < 8; ++x)
                image.at<uchar>(y, x) = 255;
        for (int y = 4; y < 6; ++y)
            for (int x = 4; x < 6; ++x)
                image.at<uchar>(y, x) = 0;
        image.at<uchar>(10, 10) = 255;
        cv::Mat gray, mask;
        cv::threshold(image, mask, 0, 255, cv::THRESH_BINARY);
        std::vector<std::vector<cv::Point>> contours;
        std::vector<cv::Vec4i>              hierarchy;
        cv::findContours(mask, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
        check(contours.size() == 3 && hierarchy.size() == 3, "findContours: square, its hole, a pixel");
        int square = -1, hole = -1, pixel = -1;
        for (int i = 0; i < static_cast<int>(contours.size()); ++i) {
            if (contours[i].size() == 1)
                pixel = i;
            else if (hierarchy[i][3] < 0)
                square = i;
            else
                hole = i;
        }
        check(square >= 0 && hole >= 0 && pixel >= 0 && hierarchy[hole][3] == square && hierarchy[square][2] == hole && hierarchy[pixel][3] == -1,
              "findContours hierarchy: the hole is the square's child");
        check(square >= 0 && contours[square].size() == 4, "CHAIN_APPROX_SIMPLE keeps the square's 4 corners");
        cv::Mat drawn = cv::Mat::zeros(12, 12, CV_8UC1);
        if (square >= 0)
            cv::drawContours(drawn, contours, square, 255, cv::FILLED);
        int filled = 0;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
                filled += drawn.at<uchar>(y, x) != 0;
        check(filled == 36, "drawContours FILLED fills the square's 36 pixels");
        if (hole >= 0)
            cv::drawContours(drawn, contours, hole, 0, cv::FILLED);
        filled = 0;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 12; ++x)
                filled += drawn.at<uchar>(y, x) != 0;
        // The hole's border is the 8 pixels of the square that touch the 2x2
        // hole by an edge: an octagon of 12 pixels with the hole.
        if (filled != 36 - 12)
            std::printf("      %d pixels left, the hole border has %d points\n", filled, hole >= 0 ? static_cast<int>(contours[hole].size()) : -1);
        check(filled == 36 - 12, "drawContours of the hole border clears the octagon it bounds");
        std::vector<std::vector<cv::Point>> all;
        cv::findContours(mask, all, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
        check(all.size() == 2, "RETR_EXTERNAL: the square and the pixel");
    }

    std::printf("%d failure(s)\n", failures);
    return failures;
}
