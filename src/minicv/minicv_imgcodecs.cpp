// minicv: image decoding (see opencv2/imgcodecs.hpp).
#include "opencv2/imgcodecs.hpp"

#include <csetjmp>
#include <cstdio>
#include <fstream>

#include <png.h>
extern "C" {
#include <jpeglib.h>
}

namespace cv {

namespace {

Mat decode_png(const uchar *bytes, size_t size, int flags)
{
    png_image image;
    std::memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes, size))
        return Mat();

    int channels = 3;
    if (flags == IMREAD_GRAYSCALE)
        channels = 1;
    else if (flags == IMREAD_UNCHANGED) {
        if (image.format & PNG_FORMAT_FLAG_ALPHA)
            channels = 4;
        else if (!(image.format & PNG_FORMAT_FLAG_COLOR))
            channels = 1;
    }
    image.format = channels == 4 ? PNG_FORMAT_BGRA : channels == 3 ? PNG_FORMAT_BGR : PNG_FORMAT_GRAY;

    Mat out(static_cast<int>(image.height), static_cast<int>(image.width), CV_MAKETYPE(CV_8U, channels));
    // without an alpha channel in the output, transparency is composed on black as OpenCV does
    png_color background = {0, 0, 0};
    if (!png_image_finish_read(&image, &background, out.data, static_cast<png_int_32>(out.step.v), nullptr)) {
        png_image_free(&image);
        return Mat();
    }
    return out;
}

struct JpegError
{
    jpeg_error_mgr base;
    std::jmp_buf   jump;
};

void jpeg_fail(j_common_ptr info)
{
    std::longjmp(reinterpret_cast<JpegError *>(info->err)->jump, 1);
}

Mat decode_jpeg(const uchar *bytes, size_t size, int flags)
{
    jpeg_decompress_struct info;
    JpegError              error;
    Mat                    out;
    info.err              = jpeg_std_error(&error.base);
    error.base.error_exit = jpeg_fail;
    if (setjmp(error.jump)) {
        jpeg_destroy_decompress(&info);
        return Mat();
    }
    jpeg_create_decompress(&info);
    jpeg_mem_src(&info, const_cast<uchar *>(bytes), static_cast<unsigned long>(size));
    jpeg_read_header(&info, TRUE);
    const bool gray = flags == IMREAD_GRAYSCALE || (flags == IMREAD_UNCHANGED && info.num_components == 1);
    info.out_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_start_decompress(&info);
    const int channels = gray ? 1 : 3;
    out.create(static_cast<int>(info.output_height), static_cast<int>(info.output_width), CV_MAKETYPE(CV_8U, channels));
    while (info.output_scanline < info.output_height) {
        JSAMPROW row = out.ptr(static_cast<int>(info.output_scanline));
        jpeg_read_scanlines(&info, &row, 1);
    }
    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    if (!gray)
        for (int y = 0; y < out.rows; ++y) {
            uchar *p = out.ptr(y);
            for (int x = 0; x < out.cols; ++x, p += 3)
                std::swap(p[0], p[2]); // RGB to BGR
        }
    return out;
}

} // namespace

Mat imdecode(const Mat &buf, int flags)
{
    if (buf.empty() || buf.depth() != CV_8U || !buf.isContinuous())
        return Mat();
    const uchar *bytes = buf.data;
    const size_t size  = buf.total() * buf.elemSize();
    if (size >= 8 && png_sig_cmp(const_cast<png_bytep>(bytes), 0, 8) == 0)
        return decode_png(bytes, size, flags);
    if (size >= 3 && bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff)
        return decode_jpeg(bytes, size, flags);
    return Mat();
}

Mat imread(const std::string &filename, int flags)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
        return Mat();
    std::vector<uchar> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty())
        return Mat();
    return imdecode(Mat(1, static_cast<int>(bytes.size()), CV_8UC1, bytes.data()), flags);
}

} // namespace cv
