#include "StereoCamera.h"

#include <algorithm>

namespace qsv
{

StereoCamera::StereoCamera()
    : m_viewW(1)
    , m_viewH(1)
    , m_zoom(1)
    , m_rotation(0)
    , m_parallax(0)
    , m_inverse(false)
    , m_sizeL(1, 1)
    , m_sizeR(1, 1)
    , m_centerL(0.5, 0.5)
    , m_centerR(0.5, 0.5)
{
}

void StereoCamera::setViewport(double widthPx, double heightPx)
{
    m_viewW = std::max(1.0, widthPx);
    m_viewH = std::max(1.0, heightPx);
}

void StereoCamera::setImageSize(Eye eye, double widthPx, double heightPx)
{
    Vec2 &sz = (eye == Eye::Left) ? m_sizeL : m_sizeR;
    sz = Vec2(std::max(1.0, widthPx), std::max(1.0, heightPx));
}

void StereoCamera::setZoom(double zoom)
{
    m_zoom = clampd(zoom, minZoom(), maxZoom());
}

void StereoCamera::setRotation(double rad)
{
    m_rotation = rad;
}

void StereoCamera::setParallax(double px)
{
    m_parallax = clampd(px, -m_viewW, m_viewW);
}

void StereoCamera::addParallax(double dpx)
{
    setParallax(m_parallax + dpx);
}

void StereoCamera::setInverseStereo(bool on)
{
    m_inverse = on;
}

void StereoCamera::setImageCenter(Eye eye, const Vec2 &c)
{
    if (eye == Eye::Left)
        m_centerL = c;
    else
        m_centerR = c;
}

Mat3 StereoCamera::imageToScreenMat(Eye eye) const
{
    const Vec2 c = (eye == Eye::Left) ? m_centerL : m_centerR;
    const double par = (eye == Eye::Right) ? m_parallax : 0.0;

    // p' = R * zoom * (p - center) + viewCenter + parallax
    const Mat3 toOrigin = Mat3::translate(-c.x, -c.y);
    const Mat3 rot = Mat3::rotate(m_rotation);
    const Mat3 sc = Mat3::scale(m_zoom, m_zoom);
    const Mat3 toView = Mat3::translate(m_viewW * 0.5 + par, m_viewH * 0.5);
    return toView * sc * rot * toOrigin;
}

Vec2 StereoCamera::imageToScreen(Eye eye, const Vec2 &image) const
{
    return imageToScreenMat(eye).map(image);
}

Vec2 StereoCamera::screenToImage(Eye eye, const Vec2 &screen) const
{
    Mat3 inv;
    if (!imageToScreenMat(eye).invert(&inv))
        return Vec2();
    return inv.map(screen);
}

Mat3 StereoCamera::imageToNdc(Eye eye) const
{
    // screen (y down) -> NDC (y up): x = x/w*2-1, y = 1-y/h*2
    const Mat3 toNdc = Mat3::translate(-1.0, 1.0) * Mat3::scale(2.0 / m_viewW, -2.0 / m_viewH);
    return toNdc * imageToScreenMat(eye);
}

void StereoCamera::zoomAtScreen(Eye eye, const Vec2 &screen, double factor)
{
    const Vec2 img = screenToImage(eye, screen);
    setZoom(m_zoom * factor);
    const Vec2 screen2 = imageToScreen(eye, img);
    panScreen(screen.x - screen2.x, screen.y - screen2.y);
}

void StereoCamera::panScreen(double dx, double dy)
{
    const double cs = std::cos(m_rotation);
    const double sn = std::sin(m_rotation);
    const Vec2 local(dx / m_zoom, dy / m_zoom);
    const Vec2 imgDelta(local.x * cs + local.y * sn, -local.x * sn + local.y * cs);
    m_centerL = m_centerL - imgDelta;
    m_centerR = m_centerR - imgDelta;
}

void StereoCamera::alignRightScreen(double dx, double dy)
{
    const double cs = std::cos(m_rotation);
    const double sn = std::sin(m_rotation);
    const Vec2 local(dx / m_zoom, dy / m_zoom);
    const Vec2 imgDelta(local.x * cs + local.y * sn, -local.x * sn + local.y * cs);
    m_centerR = m_centerR - imgDelta;
}

Vec2 StereoCamera::rightImageScreenShift() const
{
    const double cs = std::cos(m_rotation);
    const double sn = std::sin(m_rotation);
    const Vec2 d(m_centerL.x - m_centerR.x, m_centerL.y - m_centerR.y);
    const Vec2 rotated(cs * d.x - sn * d.y, sn * d.x + cs * d.y);
    return Vec2(rotated.x * m_zoom + m_parallax, rotated.y * m_zoom);
}

void StereoCamera::fitBoth()
{
    const double maxW = std::max(m_sizeL.x, m_sizeR.x);
    const double maxH = std::max(m_sizeL.y, m_sizeR.y);
    const double zx = m_viewW / maxW;
    const double zy = m_viewH / maxH;
    m_zoom = clampd(std::min(zx, zy) * 0.96, minZoom(), maxZoom());
    m_rotation = 0;
    m_parallax = 0;
    m_centerL = Vec2(m_sizeL.x * 0.5, m_sizeL.y * 0.5);
    m_centerR = Vec2(m_sizeR.x * 0.5, m_sizeR.y * 0.5);
}

void StereoCamera::reset()
{
    fitBoth();
    m_inverse = false;
}

double StereoCamera::minZoom() const
{
    const double maxW = std::max(m_sizeL.x, m_sizeR.x);
    const double maxH = std::max(m_sizeL.y, m_sizeR.y);
    const double fit = std::min(m_viewW / maxW, m_viewH / maxH);
    return std::max(0.01, fit * 0.05);
}

double StereoCamera::maxZoom() const
{
    return 256.0;
}

} // namespace qsv
