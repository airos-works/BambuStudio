// minicv: cv::Mat, the random generator and k-means (see opencv2/core.hpp).
#include "opencv2/core.hpp"

#include <algorithm>
#include <cfloat>
#include <stdexcept>

namespace cv {

RNG &theRNG()
{
    static thread_local RNG rng;
    return rng;
}

// ---------------------------------------------------------------------------
// Mat

Mat::Mat(int rows_, int cols_, int type, void *data_, size_t step_)
    : rows(rows_), cols(cols_), data(static_cast<uchar *>(data_)), m_type(type)
{
    step.v = step_ != 0 ? step_ : cols * elemSize();
}

Mat::Mat(const Mat &m, const Rect &roi) : rows(roi.height), cols(roi.width), data(nullptr), m_type(m.m_type), m_buffer(m.m_buffer)
{
    if (roi.x < 0 || roi.y < 0 || roi.width < 0 || roi.height < 0 || roi.x + roi.width > m.cols || roi.y + roi.height > m.rows)
        throw std::out_of_range("cv::Mat: the rectangle is outside of the matrix");
    step = m.step;
    data = m.data + m.step.v * roi.y + m.elemSize() * roi.x;
}

void Mat::create(int rows_, int cols_, int type)
{
    if (m_buffer && rows == rows_ && cols == cols_ && m_type == type && m_buffer.use_count() == 1 && data == m_buffer.get())
        return;
    rows   = std::max(rows_, 0);
    cols   = std::max(cols_, 0);
    m_type = type;
    step.v = cols * elemSize();
    const size_t bytes = step.v * rows;
    m_buffer.reset(bytes ? new uchar[bytes] : nullptr);
    data = m_buffer.get();
}

Mat Mat::zeros(int rows, int cols, int type)
{
    Mat m(rows, cols, type);
    if (m.data)
        std::memset(m.data, 0, m.step.v * m.rows);
    return m;
}

void Mat::copyTo(Mat &dst) const
{
    if (dst.data == data && dst.rows == rows && dst.cols == cols)
        return;
    // A destination of the right shape keeps its pixels' storage (it may be
    // a view of a larger matrix).
    if (dst.rows != rows || dst.cols != cols || dst.m_type != m_type || dst.data == nullptr)
        dst.create(rows, cols, m_type);
    const size_t row_bytes = cols * elemSize();
    for (int y = 0; y < rows; ++y)
        std::memcpy(dst.ptr(y), ptr(y), row_bytes);
}

Mat Mat::clone() const
{
    Mat m;
    copyTo(m);
    return m;
}

void Mat::setTo(const Scalar &value)
{
    const int cn = channels();
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols; ++x)
            for (int c = 0; c < cn; ++c) {
                const double v = value[c];
                switch (depth()) {
                case CV_8U: at<uchar>(y, x * cn + c) = saturate_cast<uchar>(v); break;
                case CV_32S: at<int>(y, x * cn + c) = saturate_cast<int>(v); break;
                case CV_32F: at<float>(y, x * cn + c) = static_cast<float>(v); break;
                default: throw std::invalid_argument("cv::Mat::setTo: unsupported depth");
                }
            }
}

void Mat::convertTo(Mat &dst, int rtype, double alpha, double beta) const
{
    const int ddepth = rtype < 0 ? depth() : CV_MAT_DEPTH(rtype);
    const int cn     = channels();
    Mat       out(rows, cols, CV_MAKETYPE(ddepth, cn));
    const bool scale = alpha != 1. || beta != 0.;
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < cols * cn; ++x) {
            double v;
            switch (depth()) {
            case CV_8U: v = at<uchar>(y, x); break;
            case CV_32S: v = at<int>(y, x); break;
            case CV_32F: v = at<float>(y, x); break;
            default: throw std::invalid_argument("cv::Mat::convertTo: unsupported source depth");
            }
            if (scale)
                v = v * alpha + beta;
            switch (ddepth) {
            case CV_8U: out.at<uchar>(y, x) = saturate_cast<uchar>(v); break;
            case CV_32S: out.at<int>(y, x) = saturate_cast<int>(v); break;
            case CV_32F: out.at<float>(y, x) = static_cast<float>(v); break;
            default: throw std::invalid_argument("cv::Mat::convertTo: unsupported destination depth");
            }
        }
    dst = out;
}

// ---------------------------------------------------------------------------
// k-means, after OpenCV's (modules/core/src/kmeans.cpp): the same seeding,
// iteration and termination rules, so that a seeded run clusters alike.

namespace {

inline double dist2(const float *a, const float *b, int dims)
{
    double d = 0.;
    for (int i = 0; i < dims; ++i) {
        const double t = static_cast<double>(a[i]) - b[i];
        d += t * t;
    }
    return d;
}

// k-means++ seeding (Arthur and Vassilvitskii) with OpenCV's 3 local trials.
void generate_centers_pp(const float *data, int N, int dims, std::vector<float> &centers, int K, RNG &rng)
{
    const int          trials = 3;
    std::vector<int>   chosen(K);
    std::vector<float> dist(N), tdist(N), tdist2(N);
    double             sum0 = 0.;

    chosen[0] = static_cast<int>(static_cast<unsigned>(rng) % static_cast<unsigned>(N));
    for (int i = 0; i < N; ++i) {
        dist[i] = static_cast<float>(dist2(data + size_t(i) * dims, data + size_t(chosen[0]) * dims, dims));
        sum0 += dist[i];
    }
    for (int k = 1; k < K; ++k) {
        double best_sum    = DBL_MAX;
        int    best_center = -1;
        for (int j = 0; j < trials; ++j) {
            double p  = static_cast<double>(rng) * sum0;
            int    ci = 0;
            for (; ci < N - 1; ++ci) {
                p -= dist[ci];
                if (p <= 0)
                    break;
            }
            double s = 0.;
            for (int i = 0; i < N; ++i) {
                tdist2[i] = std::min(static_cast<float>(dist2(data + size_t(i) * dims, data + size_t(ci) * dims, dims)), dist[i]);
                s += tdist2[i];
            }
            if (s < best_sum) {
                best_sum    = s;
                best_center = ci;
                std::swap(tdist, tdist2);
            }
        }
        if (best_center < 0)
            throw std::runtime_error("cv::kmeans: the samples have no finite distances");
        chosen[k] = best_center;
        sum0      = best_sum;
        std::swap(dist, tdist);
    }
    for (int k = 0; k < K; ++k)
        std::memcpy(&centers[size_t(k) * dims], data + size_t(chosen[k]) * dims, sizeof(float) * dims);
}

} // namespace

double kmeans(const Mat &data_in, int K, Mat &best_labels, TermCriteria criteria, int attempts, int flags, Mat &centers_out)
{
    if (data_in.depth() != CV_32F || data_in.empty())
        throw std::invalid_argument("cv::kmeans: the samples must be 32-bit floats");
    const int N    = data_in.rows;
    const int dims = data_in.cols * data_in.channels();
    if (K < 1 || N < K)
        throw std::invalid_argument("cv::kmeans: the number of clusters must be within [1, number of samples]");

    // the samples as one contiguous N x dims block
    std::vector<float> samples(size_t(N) * dims);
    for (int i = 0; i < N; ++i)
        std::memcpy(&samples[size_t(i) * dims], data_in.ptr(i), sizeof(float) * dims);
    const float *data = samples.data();

    attempts = std::max(attempts, 1);
    if (criteria.type & TermCriteria::EPS)
        criteria.epsilon = std::max(criteria.epsilon, 0.);
    else
        criteria.epsilon = FLT_EPSILON;
    criteria.epsilon *= criteria.epsilon;
    if (criteria.type & TermCriteria::COUNT)
        criteria.maxCount = std::min(std::max(criteria.maxCount, 2), 100);
    else
        criteria.maxCount = 100;
    if (K == 1) {
        attempts          = 1;
        criteria.maxCount = 2;
    }

    const bool initial_labels = (flags & KMEANS_USE_INITIAL_LABELS) && best_labels.rows == N && best_labels.type() == CV_32SC1;
    std::vector<int> labels(N, 0);
    if (initial_labels)
        for (int i = 0; i < N; ++i)
            labels[i] = best_labels.at<int>(i, 0);

    RNG               &rng = theRNG();
    std::vector<float> centers(size_t(K) * dims), old_centers(size_t(K) * dims), best_centers;
    std::vector<int>   counters(K), result_labels(N);
    double             best_compactness = DBL_MAX;

    for (int a = 0; a < attempts; ++a) {
        double compactness      = 0.;
        double max_center_shift = DBL_MAX;
        for (int iter = 0;;) {
            std::swap(centers, old_centers);
            if (iter == 0 && (a > 0 || !initial_labels)) {
                if (flags & KMEANS_PP_CENTERS)
                    generate_centers_pp(data, N, dims, centers, K, rng);
                else
                    for (int k = 0; k < K; ++k)
                        std::memcpy(&centers[size_t(k) * dims], data + size_t(rng.uniform(0, N)) * dims, sizeof(float) * dims);
            } else {
                // the centers are the means of their samples
                std::fill(centers.begin(), centers.end(), 0.f);
                std::fill(counters.begin(), counters.end(), 0);
                for (int i = 0; i < N; ++i) {
                    const int k = labels[i];
                    for (int d = 0; d < dims; ++d)
                        centers[size_t(k) * dims + d] += data[size_t(i) * dims + d];
                    ++counters[k];
                }
                for (int k = 0; k < K; ++k) {
                    if (counters[k] != 0)
                        continue;
                    // An empty cluster takes the sample of the biggest
                    // cluster that is the farthest from that cluster's center.
                    int max_k = 0;
                    for (int k1 = 1; k1 < K; ++k1)
                        if (counters[max_k] < counters[k1])
                            max_k = k1;
                    std::vector<float> base(dims);
                    for (int d = 0; d < dims; ++d)
                        base[d] = centers[size_t(max_k) * dims + d] * (1.f / counters[max_k]);
                    double max_dist   = 0.;
                    int    farthest_i = -1;
                    for (int i = 0; i < N; ++i) {
                        if (labels[i] != max_k)
                            continue;
                        const double dist = dist2(data + size_t(i) * dims, base.data(), dims);
                        if (max_dist <= dist) {
                            max_dist   = dist;
                            farthest_i = i;
                        }
                    }
                    --counters[max_k];
                    ++counters[k];
                    labels[farthest_i] = k;
                    for (int d = 0; d < dims; ++d) {
                        centers[size_t(max_k) * dims + d] -= data[size_t(farthest_i) * dims + d];
                        centers[size_t(k) * dims + d] += data[size_t(farthest_i) * dims + d];
                    }
                }
                for (int k = 0; k < K; ++k) {
                    const float scale = 1.f / counters[k];
                    for (int d = 0; d < dims; ++d)
                        centers[size_t(k) * dims + d] *= scale;
                }
                if (iter > 0) {
                    max_center_shift = 0.;
                    for (int k = 0; k < K; ++k)
                        max_center_shift = std::max(max_center_shift, dist2(&centers[size_t(k) * dims], &old_centers[size_t(k) * dims], dims));
                }
            }

            const bool last = (++iter == std::max(criteria.maxCount, 2)) || max_center_shift <= criteria.epsilon;
            if (last) {
                // no reassignment here: it could empty a cluster
                compactness = 0.;
                for (int i = 0; i < N; ++i)
                    compactness += dist2(data + size_t(i) * dims, &centers[size_t(labels[i]) * dims], dims);
                break;
            }
            // every sample to its nearest center
            for (int i = 0; i < N; ++i) {
                double min_dist = DBL_MAX;
                int    nearest  = 0;
                for (int k = 0; k < K; ++k) {
                    const double dist = dist2(data + size_t(i) * dims, &centers[size_t(k) * dims], dims);
                    if (min_dist > dist) {
                        min_dist = dist;
                        nearest  = k;
                    }
                }
                labels[i] = nearest;
            }
        }
        if (compactness < best_compactness) {
            best_compactness = compactness;
            best_centers     = centers;
            result_labels    = labels;
        }
    }

    best_labels = Mat(N, 1, CV_32SC1);
    for (int i = 0; i < N; ++i)
        best_labels.at<int>(i, 0) = result_labels[i];
    centers_out = Mat(K, dims, CV_32FC1);
    for (int k = 0; k < K; ++k)
        std::memcpy(centers_out.ptr(k), &best_centers[size_t(k) * dims], sizeof(float) * dims);
    return best_compactness;
}

} // namespace cv
