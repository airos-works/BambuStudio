// minicv: colour conversions, thresholding and contours (see
// opencv2/imgproc.hpp).
#include "opencv2/imgproc.hpp"

#include <algorithm>
#include <stdexcept>

namespace cv {

// ---------------------------------------------------------------------------
// cvtColor

namespace {

// sRGB companding and the D65 XYZ matrices, with OpenCV's coefficients.
inline float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float linear_to_srgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; }

const float kXn = 0.950456f, kZn = 1.088754f;

void rgb_to_lab(const uchar *rgb, uchar *lab)
{
    const float r = srgb_to_linear(rgb[0] / 255.f), g = srgb_to_linear(rgb[1] / 255.f), b = srgb_to_linear(rgb[2] / 255.f);
    const float x = (0.412453f * r + 0.357580f * g + 0.180423f * b) / kXn;
    const float y = 0.212671f * r + 0.715160f * g + 0.072169f * b;
    const float z = (0.019334f * r + 0.119193f * g + 0.950227f * b) / kZn;
    auto f = [](float t) { return t > 0.008856f ? std::cbrt(t) : 7.787f * t + 16.f / 116.f; };
    const float fx = f(x), fy = f(y), fz = f(z);
    const float L = y > 0.008856f ? 116.f * fy - 16.f : 903.3f * y;
    lab[0] = saturate_cast<uchar>(L * 255.f / 100.f);
    lab[1] = saturate_cast<uchar>(500.f * (fx - fy) + 128.f);
    lab[2] = saturate_cast<uchar>(200.f * (fy - fz) + 128.f);
}

void lab_to_rgb(const uchar *lab, uchar *rgb)
{
    const float L = lab[0] * 100.f / 255.f, a = lab[1] - 128.f, b = lab[2] - 128.f;
    float fy, y;
    if (L <= 8.f) { // 903.3 * 0.008856
        y  = L / 903.3f;
        fy = 7.787f * y + 16.f / 116.f;
    } else {
        fy = (L + 16.f) / 116.f;
        y  = fy * fy * fy;
    }
    auto finv = [](float t) { return t > 6.f / 29.f ? t * t * t : (t - 16.f / 116.f) / 7.787f; };
    const float x = finv(fy + a / 500.f) * kXn, z = finv(fy - b / 200.f) * kZn;
    const float r = 3.240479f * x - 1.537150f * y - 0.498535f * z;
    const float g = -0.969256f * x + 1.875991f * y + 0.041556f * z;
    const float bl = 0.055648f * x - 0.204043f * y + 1.057311f * z;
    auto to8 = [](float c) { return saturate_cast<uchar>(linear_to_srgb(std::min(std::max(c, 0.f), 1.f)) * 255.f); };
    rgb[0] = to8(r);
    rgb[1] = to8(g);
    rgb[2] = to8(bl);
}

void rgb_to_hsv(const uchar *rgb, uchar *hsv)
{
    const int r = rgb[0], g = rgb[1], b = rgb[2];
    const int v = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), diff = v - mn;
    float h = 0.f;
    if (diff != 0) {
        if (v == r)
            h = 60.f * (g - b) / diff;
        else if (v == g)
            h = 120.f + 60.f * (b - r) / diff;
        else
            h = 240.f + 60.f * (r - g) / diff;
        if (h < 0.f)
            h += 360.f;
    }
    int h8 = static_cast<int>(std::lrint(h / 2.f));
    if (h8 >= 180)
        h8 -= 180;
    hsv[0] = static_cast<uchar>(h8);
    hsv[1] = v == 0 ? 0 : saturate_cast<uchar>(255.f * diff / v);
    hsv[2] = static_cast<uchar>(v);
}

void hsv_to_rgb(const uchar *hsv, uchar *rgb)
{
    float h = hsv[0] * 2.f;
    const float s = hsv[1] / 255.f, v = hsv[2] / 255.f;
    float r = v, g = v, b = v;
    if (s > 0.f) {
        h = std::fmod(h, 360.f) / 60.f;
        const int   sector = static_cast<int>(h);
        const float f = h - sector, p = v * (1.f - s), q = v * (1.f - s * f), t = v * (1.f - s * (1.f - f));
        switch (sector) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
        }
    }
    rgb[0] = saturate_cast<uchar>(r * 255.f);
    rgb[1] = saturate_cast<uchar>(g * 255.f);
    rgb[2] = saturate_cast<uchar>(b * 255.f);
}

} // namespace

void cvtColor(const Mat &src, Mat &dst, int code)
{
    if (src.empty()) {
        dst = Mat();
        return;
    }
    if (src.depth() != CV_8U)
        throw std::invalid_argument("cv::cvtColor: only 8-bit images are supported");
    const int scn = src.channels();
    int       dcn = 3;
    switch (code) {
    case COLOR_BGR2BGRA: case COLOR_BGR2RGBA: case COLOR_BGRA2RGBA: dcn = 4; break;
    case COLOR_BGR2GRAY: case COLOR_RGB2GRAY: dcn = 1; break;
    default: break;
    }
    const bool four_in = code == COLOR_BGRA2BGR || code == COLOR_RGBA2BGR || code == COLOR_BGRA2RGBA;
    if (four_in ? scn != 4 : scn < 3)
        throw std::invalid_argument("cv::cvtColor: wrong number of channels for this conversion");

    Mat out(src.rows, src.cols, CV_MAKETYPE(CV_8U, dcn));
    for (int y = 0; y < src.rows; ++y) {
        const uchar *s = src.ptr(y);
        uchar       *d = out.ptr(y);
        for (int x = 0; x < src.cols; ++x, s += scn, d += dcn) {
            switch (code) {
            case COLOR_BGR2BGRA: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; break;
            case COLOR_BGRA2BGR: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; break;
            case COLOR_BGR2RGBA: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; break;
            case COLOR_RGBA2BGR: // also BGRA2RGB
            case COLOR_BGR2RGB: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; break;
            case COLOR_BGRA2RGBA: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; break;
            case COLOR_BGR2GRAY: d[0] = saturate_cast<uchar>(0.114 * s[0] + 0.587 * s[1] + 0.299 * s[2]); break;
            case COLOR_RGB2GRAY: d[0] = saturate_cast<uchar>(0.299 * s[0] + 0.587 * s[1] + 0.114 * s[2]); break;
            case COLOR_RGB2HSV: rgb_to_hsv(s, d); break;
            case COLOR_BGR2HSV: { const uchar rgb[3] = {s[2], s[1], s[0]}; rgb_to_hsv(rgb, d); break; }
            case COLOR_HSV2RGB: hsv_to_rgb(s, d); break;
            case COLOR_HSV2BGR: hsv_to_rgb(s, d); std::swap(d[0], d[2]); break;
            case COLOR_RGB2Lab: rgb_to_lab(s, d); break;
            case COLOR_BGR2Lab: { const uchar rgb[3] = {s[2], s[1], s[0]}; rgb_to_lab(rgb, d); break; }
            case COLOR_Lab2RGB: lab_to_rgb(s, d); break;
            case COLOR_Lab2BGR: lab_to_rgb(s, d); std::swap(d[0], d[2]); break;
            default: throw std::invalid_argument("cv::cvtColor: unsupported conversion");
            }
        }
    }
    dst = out;
}

double threshold(const Mat &src, Mat &dst, double thresh, double maxval, int type)
{
    if (src.type() != CV_8UC1)
        throw std::invalid_argument("cv::threshold: only 8-bit single channel images are supported");
    const uchar on = saturate_cast<uchar>(maxval);
    Mat         out(src.rows, src.cols, CV_8UC1);
    for (int y = 0; y < src.rows; ++y) {
        const uchar *s = src.ptr(y);
        uchar       *d = out.ptr(y);
        for (int x = 0; x < src.cols; ++x) {
            const bool above = s[x] > thresh;
            d[x] = (type == THRESH_BINARY_INV ? !above : above) ? on : 0;
        }
    }
    dst = out;
    return thresh;
}

// ---------------------------------------------------------------------------
// Contours: S. Suzuki, K. Abe, "Topological structural analysis of digitized
// binary images by border following", CVGIP 30 (1985), algorithm 1.

namespace {

// the 8 neighbours, clockwise from west (image rows grow downwards)
const int kDx[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
const int kDy[8] = {0, -1, -1, -1, 0, 1, 1, 1};

struct Border
{
    bool               hole;
    int                parent; // index into the border list, -1: the frame
    std::vector<Point> points;
};

inline int direction(int dy, int dx)
{
    for (int d = 0; d < 8; ++d)
        if (kDx[d] == dx && kDy[d] == dy)
            return d;
    return 0;
}

void simplify(std::vector<Point> &pts)
{
    const size_t n = pts.size();
    if (n < 3)
        return;
    std::vector<Point> out;
    for (size_t i = 0; i < n; ++i) {
        const Point &prev = pts[(i + n - 1) % n], &cur = pts[i], &next = pts[(i + 1) % n];
        if ((cur.x - prev.x) != (next.x - cur.x) || (cur.y - prev.y) != (next.y - cur.y))
            out.push_back(cur);
    }
    if (out.empty())
        out.push_back(pts[0]);
    pts.swap(out);
}

} // namespace

void findContours(const Mat &image, std::vector<std::vector<Point>> &contours, std::vector<Vec4i> &hierarchy, int mode, int method)
{
    contours.clear();
    hierarchy.clear();
    if (image.empty())
        return;
    if (image.type() != CV_8UC1)
        throw std::invalid_argument("cv::findContours: only 8-bit single channel images are supported");

    // f: the image as 0/1 inside a frame of zeros; the labels of the
    // borders are written into it as the algorithm proceeds
    const int        w = image.cols + 2, h = image.rows + 2;
    std::vector<int> f(size_t(w) * h, 0);
    for (int y = 0; y < image.rows; ++y) {
        const uchar *s = image.ptr(y);
        for (int x = 0; x < image.cols; ++x)
            f[size_t(y + 1) * w + x + 1] = s[x] != 0;
    }
    auto at = [&](int y, int x) -> int & { return f[size_t(y) * w + x]; };

    // borders[n - 2] is the border labelled n; label 1 is the frame
    std::vector<Border> borders;
    int                 nbd = 1;
    for (int i = 1; i < h - 1; ++i) {
        int lnbd = 1;
        for (int j = 1; j < w - 1; ++j) {
            if (at(i, j) == 0)
                continue;
            bool hole;
            int  i2, j2;
            if (at(i, j) == 1 && at(i, j - 1) == 0) {
                hole = false;
                i2 = i;
                j2 = j - 1;
            } else if (at(i, j) >= 1 && at(i, j + 1) == 0) {
                hole = true;
                i2 = i;
                j2 = j + 1;
                if (at(i, j) > 1)
                    lnbd = at(i, j);
            } else {
                if (at(i, j) != 1)
                    lnbd = std::abs(at(i, j));
                continue;
            }
            ++nbd;
            Border border;
            border.hole = hole;
            // the parent, from the border met last on this row
            const bool last_is_hole = lnbd == 1 || borders[lnbd - 2].hole;
            const int  last_parent  = lnbd == 1 ? -1 : borders[lnbd - 2].parent;
            border.parent = (hole == last_is_hole) ? last_parent : lnbd - 2;

            // (3.1) clockwise from (i2, j2): the first non-zero neighbour
            int start = direction(i2 - i, j2 - j), found = -1;
            for (int k = 0; k < 8; ++k) {
                const int d = (start + k) % 8;
                if (at(i + kDy[d], j + kDx[d]) != 0) {
                    found = d;
                    break;
                }
            }
            if (found < 0) {
                at(i, j) = -nbd;
                border.points.push_back(Point(j - 1, i - 1));
            } else {
                const int i1 = i + kDy[found], j1 = j + kDx[found];
                int       pi = i1, pj = j1; // (i2, j2)
                int       ci = i, cj = j;   // (i3, j3)
                for (;;) {
                    // (3.3) counterclockwise from the neighbour after (i2, j2)
                    const int from = direction(pi - ci, pj - cj);
                    bool      east_zero = false;
                    int       ni = ci, nj = cj;
                    for (int k = 1; k <= 8; ++k) {
                        const int d = (from + 8 - k) % 8;
                        if (at(ci + kDy[d], cj + kDx[d]) != 0) {
                            ni = ci + kDy[d];
                            nj = cj + kDx[d];
                            break;
                        }
                        if (d == 4)
                            east_zero = true;
                    }
                    // (3.4)
                    if (east_zero)
                        at(ci, cj) = -nbd;
                    else if (at(ci, cj) == 1)
                        at(ci, cj) = nbd;
                    border.points.push_back(Point(cj - 1, ci - 1));
                    // (3.5)
                    if (ni == i && nj == j && ci == i1 && cj == j1)
                        break;
                    pi = ci;
                    pj = cj;
                    ci = ni;
                    cj = nj;
                }
            }
            borders.push_back(std::move(border));
            if (at(i, j) != 1)
                lnbd = std::abs(at(i, j));
        }
    }

    // which borders are reported, and as children of whom
    const size_t     n = borders.size();
    std::vector<int> index(n, -1);
    for (size_t b = 0; b < n; ++b) {
        if (mode == RETR_EXTERNAL && (borders[b].hole || borders[b].parent != -1))
            continue;
        index[b] = static_cast<int>(contours.size());
        contours.push_back(borders[b].points);
        if (method != CHAIN_APPROX_NONE)
            simplify(contours.back());
    }
    hierarchy.assign(contours.size(), Vec4i(-1, -1, -1, -1));
    const bool       flat = mode == RETR_EXTERNAL || mode == RETR_LIST;
    std::vector<int> last_child(contours.size() + 1, -1); // [parent + 1]
    for (size_t b = 0; b < n; ++b) {
        const int c = index[b];
        if (c < 0)
            continue;
        const int parent = flat || borders[b].parent < 0 ? -1 : index[borders[b].parent];
        hierarchy[c][3] = parent;
        int &last = last_child[parent + 1];
        if (last >= 0) {
            hierarchy[last][0] = c;
            hierarchy[c][1]    = last;
        } else if (parent >= 0)
            hierarchy[parent][2] = c;
        last = c;
    }
}

void findContours(const Mat &image, std::vector<std::vector<Point>> &contours, int mode, int method)
{
    std::vector<Vec4i> hierarchy;
    findContours(image, contours, hierarchy, mode, method);
}

namespace {

inline void put(Mat &image, int x, int y, const Scalar &color)
{
    if (x < 0 || y < 0 || x >= image.cols || y >= image.rows)
        return;
    uchar *p = image.ptr(y) + size_t(x) * image.channels();
    for (int c = 0; c < image.channels(); ++c)
        p[c] = saturate_cast<uchar>(color[c]);
}

void line(Mat &image, Point a, Point b, const Scalar &color)
{
    const int dx = std::abs(b.x - a.x), dy = -std::abs(b.y - a.y);
    const int sx = a.x < b.x ? 1 : -1, sy = a.y < b.y ? 1 : -1;
    for (int err = dx + dy;;) {
        put(image, a.x, a.y, color);
        if (a.x == b.x && a.y == b.y)
            break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            a.x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            a.y += sy;
        }
    }
}

// even-odd scanline fill of the polygon's interior
void fill(Mat &image, const std::vector<Point> &poly, const Scalar &color)
{
    const size_t n = poly.size();
    int          y0 = image.rows, y1 = -1;
    for (const Point &p : poly) {
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    y0 = std::max(y0, 0);
    y1 = std::min(y1, image.rows - 1);
    std::vector<double> xs;
    for (int y = y0; y <= y1; ++y) {
        xs.clear();
        for (size_t i = 0; i < n; ++i) {
            const Point &a = poly[i], &b = poly[(i + 1) % n];
            if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y))
                xs.push_back(a.x + double(y - a.y) * (b.x - a.x) / (b.y - a.y));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            for (int x = static_cast<int>(std::ceil(xs[i])); x <= static_cast<int>(std::floor(xs[i + 1])); ++x)
                put(image, x, y, color);
    }
}

} // namespace

void drawContours(Mat &image, const std::vector<std::vector<Point>> &contours, int contourIdx, const Scalar &color, int thickness)
{
    if (image.depth() != CV_8U)
        throw std::invalid_argument("cv::drawContours: only 8-bit images are supported");
    const size_t first = contourIdx < 0 ? 0 : static_cast<size_t>(contourIdx);
    const size_t last  = contourIdx < 0 ? contours.size() : std::min(contours.size(), first + 1);
    for (size_t c = first; c < last; ++c) {
        const std::vector<Point> &poly = contours[c];
        if (poly.empty())
            continue;
        if (thickness < 0)
            fill(image, poly, color);
        for (size_t i = 0; i < poly.size(); ++i)
            line(image, poly[i], poly[(i + 1) % poly.size()], color);
    }
}

} // namespace cv
