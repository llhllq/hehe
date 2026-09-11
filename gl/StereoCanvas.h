#pragma once

#include "StereoCamera.h"
#include "DisplayMode.h"
#include "GpuImage.h"

#include <QOpenGLWindow>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLFramebufferObject>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QKeyEvent>
#include <QScopedPointer>
#include <QString>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QRectF>
#include <QRect>
#include <QVector>

namespace qsv
{
class ImageDecodeJob;

class StereoCanvas : public QOpenGLWindow, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    StereoCanvas();
    ~StereoCanvas();

    bool loadLeft(const QString &path);
    bool loadRight(const QString &path);
    void setLeftImage(const QImage &image, const QString &name = QString());
    void setRightImage(const QImage &image, const QString &name = QString());

    DisplayMode displayMode() const { return m_mode; }
    bool inverseStereo() const { return m_camera.inverseStereo(); }
    bool hasHardwareStereo() const { return m_hasHwStereo; }

    StereoCamera &camera() { return m_camera; }
    const StereoCamera &camera() const { return m_camera; }

    EyeAdjust &leftAdjust() { return m_adjL; }
    EyeAdjust &rightAdjust() { return m_adjR; }
    void notifyAdjustChanged();

    bool exportView(const QString &path);

    QString leftName() const { return m_nameL; }
    QString rightName() const { return m_nameR; }
    bool hasLeft() const { return m_gpuL.width() > 0 || !m_srcL.isNull(); }
    bool hasRight() const { return m_gpuR.width() > 0 || !m_srcR.isNull(); }

    bool handleDropUrls(const QList<QUrl> &urls);

public slots:
    void loadSamplePair();
    void setDisplayMode(DisplayMode mode);
    void setInverseStereo(bool on);
    void setSwapInterlace(bool on);
    void fitView();
    void resetView();
    void addParallax(double pixels);
    void zoomBy(double factor);
    void alignRightBy(double dx, double dy);

signals:
    void statusChanged(const QString &text);
    void viewChanged();
    void requestOpen(const QString &path, int eye, quint64 jobId);
    void requestWindow(int eye, quint64 jobId, int gen, int level, int x, int y, int w, int h);

private slots:
    void onImageInfo(int eye, quint64 jobId, const QSize &fullSize, bool clipOk);
    void onOverviewReady(int eye, quint64 jobId, const QImage &overview, const QSize &fullSize);
    void onPatchReady(int eye, quint64 jobId, int gen, int x, int y, int w, int h, const QImage &image);
    void onDecodeFailed(int eye, quint64 jobId, const QString &message);
    void onViewSettled();

protected:
    void initializeGL() Q_DECL_OVERRIDE;
    void resizeGL(int w, int h) Q_DECL_OVERRIDE;
    void paintGL() Q_DECL_OVERRIDE;

    void mousePressEvent(QMouseEvent *e) Q_DECL_OVERRIDE;
    void mouseMoveEvent(QMouseEvent *e) Q_DECL_OVERRIDE;
    void mouseReleaseEvent(QMouseEvent *e) Q_DECL_OVERRIDE;
    void wheelEvent(QWheelEvent *e) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *e) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent *e) Q_DECL_OVERRIDE;
    bool event(QEvent *e) Q_DECL_OVERRIDE;

private:
    enum DragMode
    {
        DragNone = 0,
        DragPan,
        DragAlign
    };

    typedef void (QOPENGLF_APIENTRYP DrawBufferProc)(GLenum);

    void emitStatus(bool force = false);
    void noteViewChanged();
    QRectF visibleImageRect(Eye eye) const;
    void ensureGpuImages();
    void syncViewTiles(int eye);
    void startFileLoad(int eye, const QString &path);
    void beginGpuForEye(int eye, int width, int height);
    GpuImage *gpuForEye(int eye);
    void applyImageSize(int eye, int width, int height, bool fitIfOnly);
    void ensureFbos();
    void unbindStereoTextures();
    void renderEye(Eye eye);
    void drawCursor(Eye eye);
    void composite();
    int compositeShaderMode() const;
    void applySwapInterval(int interval);
    void setDrawBuffer(unsigned int buffer);
    void syncCursorFromGlobal();
    void paintShutter();
    void paintComposited();
    bool peekRightImage() const;
    void drawOverlayGl();
    qreal dpr() const;
    Vec2 widgetToLogical(const QPoint &p) const;
    QImage loadImageFile(const QString &path) const;

    StereoCamera m_camera;
    DisplayMode m_mode;
    bool m_swapInterlace;
    bool m_showCursor;
    bool m_gpuDirty;
    bool m_gpuStreaming;
    bool m_shutterShowLeft;
    bool m_hasHwStereo;
    DrawBufferProc m_drawBuffer;
    int m_maxTileSize;
    quint64 m_jobL;
    quint64 m_jobR;
    quint64 m_nextJobId;
    int m_fetchGenL;
    int m_fetchGenR;
    int m_fetchLevelL;
    int m_fetchLevelR;
    QRect m_fetchRectL;
    QRect m_fetchRectR;
    QString m_loadHint;
    QString m_pathL;
    QString m_pathR;

    QImage m_srcL;
    QImage m_srcR;
    QString m_nameL;
    QString m_nameR;

    GpuImage m_gpuL;
    GpuImage m_gpuR;
    EyeAdjust m_adjL;
    EyeAdjust m_adjR;

    QOpenGLShaderProgram m_progImage;
    QOpenGLShaderProgram m_progComposite;
    QOpenGLShaderProgram m_progColor;
    QScopedPointer<QOpenGLFramebufferObject> m_fboL;
    QScopedPointer<QOpenGLFramebufferObject> m_fboR;

    DragMode m_drag;
    QPoint m_lastPos;
    QPoint m_mousePos;
    bool m_mouseInside;
    bool m_viewFetchPending;
    QTimer m_settleTimer;
    QElapsedTimer m_statusTimer;
    QVector<float> m_cursorCircle;

    QThread *m_decodeThread;
    ImageDecodeJob *m_decoder;
};

} // namespace qsv
