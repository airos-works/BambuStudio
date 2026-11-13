// minicv: colour conversions, thresholding and contours (see core.hpp).
#ifndef MINICV_OPENCV2_IMGPROC_HPP
#define MINICV_OPENCV2_IMGPROC_HPP

#include "core.hpp"

namespace cv {

// OpenCV's values; the 8-bit ranges are OpenCV's too (H 0..180, S and V
// 0..255; L 0..255, a and b offset by 128).
enum ColorConversionCodes {
    COLOR_BGR2BGRA = 0, COLOR_RGB2RGBA = COLOR_BGR2BGRA,
    COLOR_BGRA2BGR = 1, COLOR_RGBA2RGB = COLOR_BGRA2BGR,
    COLOR_BGR2RGBA = 2, COLOR_RGB2BGRA = COLOR_BGR2RGBA,
    COLOR_RGBA2BGR = 3, COLOR_BGRA2RGB = COLOR_RGBA2BGR,
    COLOR_BGR2RGB = 4, COLOR_RGB2BGR = COLOR_BGR2RGB,
    COLOR_BGRA2RGBA = 5, COLOR_RGBA2BGRA = COLOR_BGRA2RGBA,
    COLOR_BGR2GRAY = 6, COLOR_RGB2GRAY = 7,
    COLOR_BGR2HSV = 40, COLOR_RGB2HSV = 41,
    COLOR_BGR2Lab = 44, COLOR_RGB2Lab = 45,
    COLOR_HSV2BGR = 54, COLOR_HSV2RGB = 55,
    COLOR_Lab2BGR = 56, COLOR_Lab2RGB = 57
};
// 8-bit images only. src and dst may be the same matrix.
void cvtColor(const Mat &src, Mat &dst, int code);

enum ThresholdTypes { THRESH_BINARY = 0, THRESH_BINARY_INV = 1 };
// 8-bit single channel.
double threshold(const Mat &src, Mat &dst, double thresh, double maxval, int type);

enum RetrievalModes { RETR_EXTERNAL = 0, RETR_LIST = 1, RETR_CCOMP = 2, RETR_TREE = 3 };
enum ContourApproximationModes { CHAIN_APPROX_NONE = 1, CHAIN_APPROX_SIMPLE = 2, CHAIN_APPROX_TC89_L1 = 3, CHAIN_APPROX_TC89_KCOS = 4 };
// The borders of the non-zero regions of an 8-bit single channel image
// (Suzuki and Abe's border following), outer borders and holes, with
// hierarchy[i] = {next, previous, first child, parent} (-1: none).
// RETR_CCOMP is answered as RETR_TREE; the Teh-Chin approximations as
// CHAIN_APPROX_SIMPLE (every corner of the pixel outline is kept).
void findContours(const Mat &image, std::vector<std::vector<Point>> &contours, std::vector<Vec4i> &hierarchy, int mode, int method);
void findContours(const Mat &image, std::vector<std::vector<Point>> &contours, int mode, int method);

enum LineTypes { FILLED = -1, LINE_8 = 8 };
// Draws contour contourIdx (all of them if negative) into an 8-bit image:
// its outline, or with thickness FILLED the polygon it bounds.
void drawContours(Mat &image, const std::vector<std::vector<Point>> &contours, int contourIdx, const Scalar &color, int thickness = 1);

} // namespace cv

#endif
