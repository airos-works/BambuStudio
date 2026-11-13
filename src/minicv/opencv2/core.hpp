// minicv: the sliver of the OpenCV API that Bambu Studio uses, in one small
// library, for platforms where OpenCV itself is not a dependency (Haiku).
//
// What the tree needs from OpenCV is cv::Mat as an image and sample
// container, k-means colour quantisation (libslic3r/ObjColorUtils.hpp), a
// few colour conversions, PNG/JPEG decoding of embedded textures
// (libslic3r/TexturePainting.cpp) and the contours of a pick image
// (slic3r/GUI/SkipPartCanvas.cpp). The names, argument orders, constants and
// numeric conventions (BGR channel order, 8-bit HSV/Lab ranges, the RNG and
// k-means++ seeding) follow OpenCV 4, so the call sites compile unchanged.
#ifndef MINICV_OPENCV2_CORE_HPP
#define MINICV_OPENCV2_CORE_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#ifdef __HAIKU__
// uchar, ushort, int64, uint64: the same global names as OpenCV's
#include <SupportDefs.h>
#else
typedef unsigned char  uchar;
typedef unsigned short ushort;
typedef std::int64_t   int64;
typedef std::uint64_t  uint64;
#endif

#define CV_8U 0
#define CV_8S 1
#define CV_16U 2
#define CV_16S 3
#define CV_32S 4
#define CV_32F 5
#define CV_64F 6
#define CV_CN_SHIFT 3
#define CV_MAT_DEPTH(type) ((type) & 7)
#define CV_MAT_CN(type) ((((type) >> CV_CN_SHIFT) & 63) + 1)
#define CV_MAKETYPE(depth, cn) (CV_MAT_DEPTH(depth) + (((cn) - 1) << CV_CN_SHIFT))
#define CV_8UC1 CV_MAKETYPE(CV_8U, 1)
#define CV_8UC3 CV_MAKETYPE(CV_8U, 3)
#define CV_8UC4 CV_MAKETYPE(CV_8U, 4)
#define CV_32SC1 CV_MAKETYPE(CV_32S, 1)
#define CV_32FC1 CV_MAKETYPE(CV_32F, 1)
#define CV_32FC3 CV_MAKETYPE(CV_32F, 3)

namespace cv {

// Rounds to nearest and clamps to the range of the target, as OpenCV does.
template<class T> inline T saturate_cast(double v) { return static_cast<T>(v); }
template<> inline uchar saturate_cast<uchar>(double v)
{
    const long r = std::lrint(v);
    return static_cast<uchar>(r < 0 ? 0 : r > 255 ? 255 : r);
}
template<> inline int saturate_cast<int>(double v) { return static_cast<int>(std::lrint(v)); }

template<class T, int N> class Vec
{
public:
    typedef T value_type;
    enum { channels = N };
    T val[N];

    Vec() { for (int i = 0; i < N; ++i) val[i] = T(); }
    Vec(T v0, T v1) : Vec() { val[0] = v0; val[1] = v1; }
    Vec(T v0, T v1, T v2) : Vec() { val[0] = v0; val[1] = v1; val[2] = v2; }
    Vec(T v0, T v1, T v2, T v3) : Vec() { val[0] = v0; val[1] = v1; val[2] = v2; val[3] = v3; }
    template<class U> Vec(const Vec<U, N> &o) { for (int i = 0; i < N; ++i) val[i] = saturate_cast<T>(o.val[i]); }

    T       &operator[](int i) { return val[i]; }
    const T &operator[](int i) const { return val[i]; }
    bool operator==(const Vec &o) const { for (int i = 0; i < N; ++i) if (val[i] != o.val[i]) return false; return true; }
    bool operator!=(const Vec &o) const { return !(*this == o); }
};
typedef Vec<uchar, 3> Vec3b;
typedef Vec<uchar, 4> Vec4b;
typedef Vec<float, 3> Vec3f;
typedef Vec<int, 4>   Vec4i;

template<class T> struct Point_
{
    T x, y;
    Point_() : x(0), y(0) {}
    Point_(T x_, T y_) : x(x_), y(y_) {}
    bool operator==(const Point_ &o) const { return x == o.x && y == o.y; }
    bool operator!=(const Point_ &o) const { return !(*this == o); }
};
typedef Point_<int>   Point;
typedef Point_<float> Point2f;

struct Size
{
    int width, height;
    Size() : width(0), height(0) {}
    Size(int w, int h) : width(w), height(h) {}
    bool operator==(const Size &o) const { return width == o.width && height == o.height; }
};

struct Rect
{
    int x, y, width, height;
    Rect() : x(0), y(0), width(0), height(0) {}
    Rect(int x_, int y_, int w, int h) : x(x_), y(y_), width(w), height(h) {}
};

struct Scalar
{
    double val[4];
    Scalar(double v0 = 0, double v1 = 0, double v2 = 0, double v3 = 0) { val[0] = v0; val[1] = v1; val[2] = v2; val[3] = v3; }
    double operator[](int i) const { return val[i]; }
    static Scalar all(double v) { return Scalar(v, v, v, v); }
};

struct TermCriteria
{
    enum Type { COUNT = 1, MAX_ITER = COUNT, EPS = 2 };
    int    type;
    int    maxCount;
    double epsilon;
    TermCriteria() : type(0), maxCount(0), epsilon(0) {}
    TermCriteria(int type_, int maxCount_, double epsilon_) : type(type_), maxCount(maxCount_), epsilon(epsilon_) {}
};

// OpenCV's multiply-with-carry generator, bit for bit: seeding it gives the
// k-means++ picks OpenCV would make.
class RNG
{
public:
    uint64 state;
    RNG() : state(0xffffffff) {}
    explicit RNG(uint64 s) : state(s ? s : 0xffffffff) {}
    unsigned next()
    {
        state = static_cast<uint64>(static_cast<unsigned>(state)) * 4164903690U + static_cast<unsigned>(state >> 32);
        return static_cast<unsigned>(state);
    }
    operator unsigned() { return next(); }
    operator double()
    {
        const unsigned t = next();
        return static_cast<double>((static_cast<uint64>(t) << 32) | next()) * 5.4210108624275221700372640043497e-20;
    }
    int    uniform(int a, int b) { return a == b ? a : static_cast<int>(next() % static_cast<unsigned>(b - a)) + a; }
    double uniform(double a, double b) { return static_cast<double>(*this) * (b - a) + a; }
};
// The generator of the calling thread.
RNG &theRNG();

// A reference-counted dense matrix of rows x cols elements of 1 to 4
// channels: 8-bit unsigned, 32-bit signed or 32-bit float. Copies share the
// pixels; clone() and copyTo() duplicate them.
class Mat
{
public:
    // step[0]: bytes from one row to the next
    struct Step
    {
        size_t v;
        Step() : v(0) {}
        operator size_t() const { return v; }
        size_t operator[](int i) const { return i == 0 ? v : 0; }
    };

    int    rows, cols;
    uchar *data;
    Step   step;

    Mat() : rows(0), cols(0), data(nullptr), m_type(0) {}
    Mat(int rows_, int cols_, int type) : Mat() { create(rows_, cols_, type); }
    Mat(Size size, int type) : Mat() { create(size.height, size.width, type); }
    // The caller's pixels, not copied and not owned.
    Mat(int rows_, int cols_, int type, void *data_, size_t step_ = 0);
    // A view of a rectangle of m.
    Mat(const Mat &m, const Rect &roi);

    static Mat zeros(int rows, int cols, int type);
    static Mat zeros(Size size, int type) { return zeros(size.height, size.width, type); }

    void create(int rows_, int cols_, int type);
    Mat  clone() const;
    void copyTo(Mat &dst) const;
    // dst = saturate(this * alpha + beta) in the depth of rtype (-1: this depth).
    void convertTo(Mat &dst, int rtype, double alpha = 1., double beta = 0.) const;
    Mat  operator()(const Rect &roi) const { return Mat(*this, roi); }
    Mat  row(int y) const { return Mat(*this, Rect(0, y, cols, 1)); }
    void setTo(const Scalar &value);

    int    type() const { return m_type; }
    int    depth() const { return CV_MAT_DEPTH(m_type); }
    int    channels() const { return CV_MAT_CN(m_type); }
    size_t elemSize1() const { return depth() == CV_8U || depth() == CV_8S ? 1 : depth() == CV_64F ? 8 : depth() <= CV_16S ? 2 : 4; }
    size_t elemSize() const { return elemSize1() * channels(); }
    bool   empty() const { return data == nullptr || rows == 0 || cols == 0; }
    size_t total() const { return static_cast<size_t>(rows) * cols; }
    Size   size() const { return Size(cols, rows); }
    bool   isContinuous() const { return rows <= 1 || step.v == cols * elemSize(); }

    uchar       *ptr(int y = 0) { return data + step.v * y; }
    const uchar *ptr(int y = 0) const { return data + step.v * y; }
    template<class T> T       *ptr(int y = 0) { return reinterpret_cast<T *>(data + step.v * y); }
    template<class T> const T *ptr(int y = 0) const { return reinterpret_cast<const T *>(data + step.v * y); }

    template<class T> T       &at(int y, int x) { return reinterpret_cast<T *>(data + step.v * y)[x]; }
    template<class T> const T &at(int y, int x) const { return reinterpret_cast<const T *>(data + step.v * y)[x]; }
    // The i-th element of type T of a continuous matrix (or of a single column).
    template<class T> T       &at(int i) { return isContinuous() ? reinterpret_cast<T *>(data)[i] : *reinterpret_cast<T *>(data + step.v * i); }
    template<class T> const T &at(int i) const { return isContinuous() ? reinterpret_cast<const T *>(data)[i] : *reinterpret_cast<const T *>(data + step.v * i); }
    template<class T> T       &at(Point p) { return at<T>(p.y, p.x); }
    template<class T> const T &at(Point p) const { return at<T>(p.y, p.x); }

private:
    int                      m_type;
    std::shared_ptr<uchar[]> m_buffer;
};

enum KmeansFlags { KMEANS_RANDOM_CENTERS = 0, KMEANS_PP_CENTERS = 2, KMEANS_USE_INITIAL_LABELS = 1 };

// Clusters the rows of data (32-bit float; N x 1 with D channels or N x D)
// into K clusters with Lloyd's algorithm, `attempts` times from different
// seeds, keeping the most compact result. bestLabels: N x 1 CV_32S. centers:
// K x D CV_32F. Returns the compactness (sum of squared distances).
double kmeans(const Mat &data, int K, Mat &bestLabels, TermCriteria criteria, int attempts, int flags, Mat &centers);

} // namespace cv

#endif
