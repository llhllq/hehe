#pragma once

#include <cmath>
#include <algorithm>

namespace qsv
{

struct Vec2
{
    double x;
    double y;

    Vec2() : x(0), y(0) {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2 &o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator-(const Vec2 &o) const { return Vec2(x - o.x, y - o.y); }
    Vec2 operator*(double s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(double s) const { return Vec2(x / s, y / s); }
    Vec2 &operator+=(const Vec2 &o) { x += o.x; y += o.y; return *this; }
};

inline Vec2 operator*(double s, const Vec2 &v) { return v * s; }

// 行主序 3x3，用于影像像素(y 向下)到 NDC(y 向上) 的仿射变换。
struct Mat3
{
    double m[9];

    static Mat3 identity()
    {
        Mat3 r;
        r.m[0] = 1; r.m[1] = 0; r.m[2] = 0;
        r.m[3] = 0; r.m[4] = 1; r.m[5] = 0;
        r.m[6] = 0; r.m[7] = 0; r.m[8] = 1;
        return r;
    }

    static Mat3 translate(double tx, double ty)
    {
        Mat3 r = identity();
        r.m[2] = tx;
        r.m[5] = ty;
        return r;
    }

    static Mat3 scale(double sx, double sy)
    {
        Mat3 r = identity();
        r.m[0] = sx;
        r.m[4] = sy;
        return r;
    }

    static Mat3 rotate(double rad)
    {
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        Mat3 r = identity();
        r.m[0] = c;  r.m[1] = -s;
        r.m[3] = s;  r.m[4] =  c;
        return r;
    }

    Mat3 operator*(const Mat3 &b) const
    {
        Mat3 r;
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 3; ++col)
            {
                r.m[row * 3 + col] =
                    m[row * 3 + 0] * b.m[0 * 3 + col] +
                    m[row * 3 + 1] * b.m[1 * 3 + col] +
                    m[row * 3 + 2] * b.m[2 * 3 + col];
            }
        }
        return r;
    }

    Vec2 map(const Vec2 &p) const
    {
        const double x = m[0] * p.x + m[1] * p.y + m[2];
        const double y = m[3] * p.x + m[4] * p.y + m[5];
        const double w = m[6] * p.x + m[7] * p.y + m[8];
        if (std::fabs(w) < 1e-12)
            return Vec2(x, y);
        return Vec2(x / w, y / w);
    }

    bool invert(Mat3 *out) const
    {
        const double a = m[0], b = m[1], c = m[2];
        const double d = m[3], e = m[4], f = m[5];
        const double g = m[6], h = m[7], i = m[8];
        const double A = e * i - f * h;
        const double B = f * g - d * i;
        const double C = d * h - e * g;
        const double det = a * A + b * B + c * C;
        if (std::fabs(det) < 1e-14)
            return false;
        const double inv = 1.0 / det;
        out->m[0] = A * inv;
        out->m[1] = (c * h - b * i) * inv;
        out->m[2] = (b * f - c * e) * inv;
        out->m[3] = B * inv;
        out->m[4] = (a * i - c * g) * inv;
        out->m[5] = (c * d - a * f) * inv;
        out->m[6] = C * inv;
        out->m[7] = (b * g - a * h) * inv;
        out->m[8] = (a * e - b * d) * inv;
        return true;
    }

    void toRowMajorFloat(float out[9]) const
    {
        for (int i = 0; i < 9; ++i)
            out[i] = float(m[i]);
    }

    void toColumnMajorFloat(float out[9]) const
    {
        out[0] = float(m[0]); out[1] = float(m[3]); out[2] = float(m[6]);
        out[3] = float(m[1]); out[4] = float(m[4]); out[5] = float(m[7]);
        out[6] = float(m[2]); out[7] = float(m[5]); out[8] = float(m[8]);
    }
};

enum class Eye
{
    Left = 0,
    Right = 1
};

inline double clampd(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

} // namespace qsv
