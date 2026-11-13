// minicv: PNG and JPEG decoding through the system's libpng and libjpeg
// (see core.hpp).
#ifndef MINICV_OPENCV2_IMGCODECS_HPP
#define MINICV_OPENCV2_IMGCODECS_HPP

#include "core.hpp"

namespace cv {

enum ImreadModes { IMREAD_UNCHANGED = -1, IMREAD_GRAYSCALE = 0, IMREAD_COLOR = 1 };

// Decodes a PNG or JPEG file image held in buf (any shape, 8-bit).
// IMREAD_COLOR: 3 channels, BGR. IMREAD_UNCHANGED: BGRA if the image has an
// alpha channel, one channel if it is greyscale, else BGR.
// IMREAD_GRAYSCALE: one channel. An empty matrix if it cannot be decoded.
Mat imdecode(const Mat &buf, int flags);
Mat imread(const std::string &filename, int flags = IMREAD_COLOR);

} // namespace cv

#endif
