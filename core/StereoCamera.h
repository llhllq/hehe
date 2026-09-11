#pragma once

#include "StereoMath.h"

namespace qsv
{

// 左右眼独立锚点 + 公共缩放/旋转 + 右眼水平视差。
// 不依赖窗口系统，便于跨平台和单测。
class StereoCamera
{
public:
    StereoCamera();

    void setViewport(double widthPx, double heightPx);
    void setImageSize(Eye eye, double widthPx, double heightPx);

    double viewportWidth() const { return m_viewW; }
    double viewportHeight() const { return m_viewH; }
    double zoom() const { return m_zoom; }
    double rotation() const { return m_rotation; }
    double parallax() const { return m_parallax; }
    bool inverseStereo() const { return m_inverse; }

    Vec2 imageSize(Eye eye) const { return eye == Eye::Left ? m_sizeL : m_sizeR; }
    Vec2 imageCenter(Eye eye) const { return eye == Eye::Left ? m_centerL : m_centerR; }

    // 右片相对左片的屏幕偏移（含视差），用于界面反馈。
    Vec2 rightImageScreenShift() const;

    void setZoom(double zoom);
    void setRotation(double rad);
    void setParallax(double px);
    void addParallax(double dpx);
    void setInverseStereo(bool on);
    void setImageCenter(Eye eye, const Vec2 &c);

    // 让指定影像点落在视口某像素上（用于光标处缩放）。
    void zoomAtScreen(Eye eye, const Vec2 &screen, double factor);
    void panScreen(double dx, double dy);
    void alignRightScreen(double dx, double dy);

    void fitBoth();
    void reset();

    // 影像像素 -> 视口像素（y 向下）
    Vec2 imageToScreen(Eye eye, const Vec2 &image) const;
    Vec2 screenToImage(Eye eye, const Vec2 &screen) const;

    // 影像像素 -> NDC，供 GPU 使用
    Mat3 imageToNdc(Eye eye) const;

    double minZoom() const;
    double maxZoom() const;

private:
    Mat3 imageToScreenMat(Eye eye) const;

    double m_viewW;
    double m_viewH;
    double m_zoom;
    double m_rotation;
    double m_parallax;   // 右眼屏幕 X 偏移，对应“量测高程/视差”
    bool m_inverse;
    Vec2 m_sizeL;
    Vec2 m_sizeR;
    Vec2 m_centerL;      // 落在视口中心的左影像点
    Vec2 m_centerR;
};

} // namespace qsv
