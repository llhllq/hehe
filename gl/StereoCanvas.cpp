#include "StereoCanvas.h"
#include "GlShader.h"
#include "SampleImages.h"
#include "ImageDecodeJob.h"

#include <QEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QUrl>
#include <QCursor>
#include <QGuiApplication>
#include <QColor>
#include <QThread>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObjectFormat>
#include <QSurfaceFormat>
#include <QtMath>
#include <QLibrary>
#include <QDebug>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include <QSize>
#include <algorithm>
#include <cmath>

#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT 0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT 0x0403
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif

namespace qsv
{

StereoCanvas::StereoCanvas()
    : QOpenGLWindow(QOpenGLWindow::NoPartialUpdate)
    , m_mode(DisplayMode::Shutter)
    , m_swapInterlace(false)
    , m_showCursor(true)
    , m_gpuDirty(false)
    , m_gpuStreaming(false)
    , m_shutterShowLeft(true)
    , m_hasHwStereo(false)
    , m_drawBuffer(0)
    , m_maxTileSize(2048)
    , m_jobL(0)
    , m_jobR(0)
    , m_nextJobId(1)
    , m_fetchGenL(0)
    , m_fetchGenR(0)
    , m_fetchLevelL(-1)
    , m_fetchLevelR(-1)
    , m_drag(DragNone)
    , m_mouseInside(false)
    , m_viewFetchPending(false)
    , m_decodeThread(new QThread(this))
    , m_decoder(new ImageDecodeJob)
{
    QSurfaceFormat fmt = requestedFormat();
    fmt.setDepthBufferSize(0);
    fmt.setStencilBufferSize(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(2, 1);
    fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    fmt.setStereo(true);
    fmt.setSwapInterval(0);
    setFormat(fmt);

    setMinimumSize(QSize(320, 240));
    setCursor(Qt::BlankCursor);
    m_statusTimer.start();

    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(150);
    connect(&m_settleTimer, SIGNAL(timeout()), this, SLOT(onViewSettled()));

    m_decoder->moveToThread(m_decodeThread);
    connect(this, SIGNAL(requestOpen(QString,int,quint64)),
            m_decoder, SLOT(openFile(QString,int,quint64)), Qt::QueuedConnection);
    connect(this, SIGNAL(requestWindow(int,quint64,int,int,int,int,int,int)),
            m_decoder, SLOT(fetchWindow(int,quint64,int,int,int,int,int,int)), Qt::QueuedConnection);
    connect(m_decoder, SIGNAL(imageInfo(int,quint64,QSize,bool)),
            this, SLOT(onImageInfo(int,quint64,QSize,bool)), Qt::QueuedConnection);
    connect(m_decoder, SIGNAL(overviewReady(int,quint64,QImage,QSize)),
            this, SLOT(onOverviewReady(int,quint64,QImage,QSize)), Qt::QueuedConnection);
    connect(m_decoder, SIGNAL(patchReady(int,quint64,int,int,int,int,int,QImage)),
            this, SLOT(onPatchReady(int,quint64,int,int,int,int,int,QImage)), Qt::QueuedConnection);
    connect(m_decoder, SIGNAL(failed(int,quint64,QString)),
            this, SLOT(onDecodeFailed(int,quint64,QString)), Qt::QueuedConnection);
    m_decodeThread->start();

    connect(this, &QOpenGLWindow::frameSwapped, this, [this]() {
        if (!isExposed())
            return;
        if ((m_mode == DisplayMode::Shutter && !m_hasHwStereo) || m_gpuStreaming)
            requestUpdate();
    });
}

StereoCanvas::~StereoCanvas()
{
    if (m_decoder)
        m_decoder->requestCancel();
    if (m_decodeThread)
    {
        m_decodeThread->quit();
        m_decodeThread->wait(4000);
    }
    if (context())
    {
        makeCurrent();
        unbindStereoTextures();
        m_gpuL.destroy();
        m_gpuR.destroy();
        m_fboL.reset();
        m_fboR.reset();
        doneCurrent();
    }
    delete m_decoder;
    m_decoder = 0;
}

qreal StereoCanvas::dpr() const
{
    return std::max(qreal(1.0), devicePixelRatio());
}

bool StereoCanvas::loadLeft(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    startFileLoad(0, path);
    return true;
}

bool StereoCanvas::loadRight(const QString &path)
{
    if (!QFileInfo::exists(path))
        return false;
    startFileLoad(1, path);
    return true;
}

void StereoCanvas::startFileLoad(int eye, const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    if (eye == 0)
    {
        ++m_nextJobId;
        m_jobL = m_nextJobId;
        m_nameL = name;
        m_srcL = QImage();
        m_pathL = path;
        m_fetchGenL = 0;
        m_fetchLevelL = -1;
        m_fetchRectL = QRect();
        m_loadHint = QString::fromUtf8("正在读取左片 %1 ...").arg(name);
    }
    else
    {
        ++m_nextJobId;
        m_jobR = m_nextJobId;
        m_nameR = name;
        m_srcR = QImage();
        m_pathR = path;
        m_fetchGenR = 0;
        m_fetchLevelR = -1;
        m_fetchRectR = QRect();
        m_loadHint = QString::fromUtf8("正在读取右片 %1 ...").arg(name);
    }
    if (m_decoder)
        m_decoder->bumpWindow(eye);
    if (context() && context()->isValid())
    {
        makeCurrent();
        gpuForEye(eye)->destroy();
        doneCurrent();
    }
    emitStatus(true);
    emit requestOpen(path, eye, eye == 0 ? m_jobL : m_jobR);
    requestUpdate();
}

void StereoCanvas::beginGpuForEye(int eye, int width, int height)
{
    GpuImage *gpu = gpuForEye(eye);
    if (!gpu)
        return;
    gpu->reset(width, height);
}

GpuImage *StereoCanvas::gpuForEye(int eye)
{
    return eye == 0 ? &m_gpuL : &m_gpuR;
}

void StereoCanvas::applyImageSize(int eye, int width, int height, bool fitIfOnly)
{
    if (eye == 0)
        m_camera.setImageSize(Eye::Left, width, height);
    else
        m_camera.setImageSize(Eye::Right, width, height);
    if (fitIfOnly)
    {
        if ((eye == 0 && m_gpuR.width() <= 0 && m_srcR.isNull())
            || (eye == 1 && m_gpuL.width() <= 0 && m_srcL.isNull()))
            m_camera.fitBoth();
    }
}

void StereoCanvas::setLeftImage(const QImage &image, const QString &name)
{
    m_jobL = 0;
    m_pathL.clear();
    m_srcL = image;
    m_nameL = name.isEmpty() ? QString::fromUtf8("左片") : name;
    m_camera.setImageSize(Eye::Left, image.width(), image.height());
    m_gpuDirty = true;
    m_loadHint.clear();
    if (m_srcR.isNull() && m_gpuR.width() <= 0)
        m_camera.fitBoth();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::setRightImage(const QImage &image, const QString &name)
{
    m_jobR = 0;
    m_pathR.clear();
    m_srcR = image;
    m_nameR = name.isEmpty() ? QString::fromUtf8("右片") : name;
    m_camera.setImageSize(Eye::Right, image.width(), image.height());
    m_gpuDirty = true;
    m_loadHint.clear();
    if (m_srcL.isNull() && m_gpuL.width() <= 0)
        m_camera.fitBoth();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::loadSamplePair()
{
    QImage L, R;
    makeSampleStereoPair(&L, &R);
    m_jobL = 0;
    m_jobR = 0;
    m_pathL.clear();
    m_pathR.clear();
    m_srcL = L;
    m_srcR = R;
    m_nameL = QString::fromUtf8("示例左片");
    m_nameR = QString::fromUtf8("示例右片");
    m_camera.setImageSize(Eye::Left, L.width(), L.height());
    m_camera.setImageSize(Eye::Right, R.width(), R.height());
    m_camera.fitBoth();
    m_gpuDirty = true;
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::setDisplayMode(DisplayMode mode)
{
    m_mode = mode;
    if (context() && context()->isValid())
    {
        makeCurrent();
        unbindStereoTextures();
        m_fboL.reset();
        m_fboR.reset();
        applySwapInterval((mode == DisplayMode::Shutter && m_hasHwStereo) ? 1 : 0);
        doneCurrent();
    }
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::setInverseStereo(bool on)
{
    m_camera.setInverseStereo(on);
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::setSwapInterlace(bool on)
{
    m_swapInterlace = on;
    requestUpdate();
}

void StereoCanvas::notifyAdjustChanged()
{
    requestUpdate();
}

void StereoCanvas::fitView()
{
    m_camera.fitBoth();
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::resetView()
{
    m_camera.reset();
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

bool StereoCanvas::exportView(const QString &path)
{
    QImage img = grabFramebuffer();
    return img.save(path);
}

void StereoCanvas::addParallax(double pixels)
{
    m_camera.addParallax(pixels);
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::zoomBy(double factor)
{
    const Vec2 center(m_camera.viewportWidth() * 0.5, m_camera.viewportHeight() * 0.5);
    m_camera.zoomAtScreen(Eye::Left, center, factor);
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::alignRightBy(double dx, double dy)
{
    m_camera.alignRightScreen(dx, dy);
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::applySwapInterval(int interval)
{
    if (!context())
        return;
    QFunctionPointer p = context()->getProcAddress("wglSwapIntervalEXT");
    if (p)
    {
        typedef int (*Fn)(int);
        reinterpret_cast<Fn>(p)(interval);
        return;
    }
    p = context()->getProcAddress("glXSwapIntervalSGI");
    if (p)
    {
        typedef int (*Fn)(int);
        reinterpret_cast<Fn>(p)(interval);
    }
}

void StereoCanvas::setDrawBuffer(unsigned int buffer)
{
    if (m_drawBuffer)
        m_drawBuffer(buffer);
}

void StereoCanvas::initializeGL()
{
    initializeOpenGLFunctions();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLint maxTex = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    if (maxTex < 64)
        maxTex = 2048;
    m_maxTileSize = std::min(2048, std::max(64, int(maxTex)));

    m_hasHwStereo = context() && context()->format().stereo();
    m_drawBuffer = reinterpret_cast<DrawBufferProc>(
        context()->getProcAddress(QByteArrayLiteral("glDrawBuffer")));
    if (!m_drawBuffer)
        m_drawBuffer = reinterpret_cast<DrawBufferProc>(
            context()->getProcAddress(QByteArrayLiteral("glDrawBufferARB")));
    if (!m_drawBuffer)
    {
#ifdef Q_OS_WIN
        QLibrary glLib(QStringLiteral("opengl32"));
#else
        QLibrary glLib(QStringLiteral("GL"));
#endif
        m_drawBuffer = reinterpret_cast<DrawBufferProc>(glLib.resolve("glDrawBuffer"));
    }

    applySwapInterval((m_mode == DisplayMode::Shutter && m_hasHwStereo) ? 1 : 0);

    QString err;
    m_progImage.bindAttributeLocation("a_pos", 0);
    if (!loadShaderFromQrc(&m_progImage, QStringLiteral(":/qsv/shaders/image.vert"),
                           QStringLiteral(":/qsv/shaders/image.frag"), &err))
        qWarning() << "image shader:" << err;

    m_progComposite.bindAttributeLocation("a_pos", 0);
    if (!loadShaderFromQrc(&m_progComposite, QStringLiteral(":/qsv/shaders/composite.vert"),
                           QStringLiteral(":/qsv/shaders/composite.frag"), &err))
        qWarning() << "composite shader:" << err;

    m_progColor.bindAttributeLocation("a_pos", 0);
    if (!loadShaderFromQrc(&m_progColor, QStringLiteral(":/qsv/shaders/color.vert"),
                           QStringLiteral(":/qsv/shaders/color.frag"), &err))
        qWarning() << "color shader:" << err;

    m_gpuDirty = true;
    qWarning() << "QStereoView hardware stereo:" << m_hasHwStereo;
}

void StereoCanvas::resizeGL(int w, int h)
{
    unbindStereoTextures();
    m_camera.setViewport(std::max(1, w), std::max(1, h));
    m_fboL.reset();
    m_fboR.reset();
    noteViewChanged();
}

void StereoCanvas::unbindStereoTextures()
{
    if (!isValid())
        return;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

int StereoCanvas::compositeShaderMode() const
{
    if (m_mode == DisplayMode::Shutter)
        return m_shutterShowLeft ? int(DisplayMode::LeftOnly) : int(DisplayMode::RightOnly);
    return int(m_mode);
}

void StereoCanvas::noteViewChanged()
{
    m_viewFetchPending = false;
    m_settleTimer.start();
}

void StereoCanvas::onViewSettled()
{
    m_viewFetchPending = true;
    requestUpdate();
}

QRectF StereoCanvas::visibleImageRect(Eye eye) const
{
    const double w = std::max(1.0, m_camera.viewportWidth());
    const double h = std::max(1.0, m_camera.viewportHeight());
    const Vec2 c0 = m_camera.screenToImage(eye, Vec2(0, 0));
    const Vec2 c1 = m_camera.screenToImage(eye, Vec2(w, 0));
    const Vec2 c2 = m_camera.screenToImage(eye, Vec2(w, h));
    const Vec2 c3 = m_camera.screenToImage(eye, Vec2(0, h));
    const double x0 = std::min(std::min(c0.x, c1.x), std::min(c2.x, c3.x));
    const double x1 = std::max(std::max(c0.x, c1.x), std::max(c2.x, c3.x));
    const double y0 = std::min(std::min(c0.y, c1.y), std::min(c2.y, c3.y));
    const double y1 = std::max(std::max(c0.y, c1.y), std::max(c2.y, c3.y));
    const double pad = 256.0;
    return QRectF(x0 - pad, y0 - pad, (x1 - x0) + 2.0 * pad, (y1 - y0) + 2.0 * pad);
}

void StereoCanvas::syncCursorFromGlobal()
{
    const QPoint local = mapFromGlobal(QCursor::pos());
    if (local.x() >= 0 && local.y() >= 0 && local.x() < width() && local.y() < height())
    {
        m_mousePos = local;
        m_mouseInside = true;
    }
    else
    {
        m_mouseInside = false;
    }
}

void StereoCanvas::paintGL()
{
    unbindStereoTextures();
    ensureGpuImages();
    syncCursorFromGlobal();

    if (m_mode == DisplayMode::Shutter)
        paintShutter();
    else
        paintComposited();
}

bool StereoCanvas::peekRightImage() const
{
    if (m_drag == DragAlign)
        return true;
    const Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    return (mods & (Qt::ShiftModifier | Qt::AltModifier)) != 0;
}

void StereoCanvas::paintShutter()
{
    const int pw = std::max(1, int(std::ceil(width() * dpr())));
    const int ph = std::max(1, int(std::ceil(height() * dpr())));
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const bool inverse = m_camera.inverseStereo();
    const Eye first = inverse ? Eye::Right : Eye::Left;
    const Eye second = inverse ? Eye::Left : Eye::Right;
    const bool peekRight = peekRightImage();

    if (m_hasHwStereo && m_drawBuffer)
    {
        setDrawBuffer(GL_BACK_LEFT);
        glViewport(0, 0, pw, ph);
        renderEye(first);
        setDrawBuffer(GL_BACK_RIGHT);
        glViewport(0, 0, pw, ph);
        renderEye(second);
        if (peekRight)
        {
            // 无眼镜时屏幕通常只显示左缓冲，按住 Shift/Alt 时把右片画到可见缓冲。
            setDrawBuffer(GL_BACK_LEFT);
            glViewport(0, 0, pw, ph);
            renderEye(second);
            drawOverlayGl();
        }
        else
        {
            setDrawBuffer(GL_BACK_LEFT);
            drawOverlayGl();
            setDrawBuffer(GL_BACK_RIGHT);
            drawOverlayGl();
        }
        setDrawBuffer(GL_BACK);
        glEnable(GL_BLEND);
        return;
    }

    glViewport(0, 0, pw, ph);
    if (peekRight)
        renderEye(second);
    else
    {
        renderEye(m_shutterShowLeft ? first : second);
        m_shutterShowLeft = !m_shutterShowLeft;
    }
    drawOverlayGl();
    glEnable(GL_BLEND);
}

void StereoCanvas::paintComposited()
{
    ensureFbos();
    if (m_fboL.isNull() || m_fboR.isNull())
        return;

    m_fboL->bind();
    renderEye(Eye::Left);
    m_fboL->release();
    unbindStereoTextures();

    m_fboR->bind();
    renderEye(Eye::Right);
    m_fboR->release();
    unbindStereoTextures();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const int pw = std::max(1, int(std::ceil(width() * dpr())));
    const int ph = std::max(1, int(std::ceil(height() * dpr())));
    glViewport(0, 0, pw, ph);
    glDisable(GL_BLEND);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    composite();
    unbindStereoTextures();
    glEnable(GL_BLEND);
    drawOverlayGl();
}

void StereoCanvas::ensureGpuImages()
{
    if (m_gpuDirty)
    {
        if (!m_srcL.isNull())
        {
            m_gpuL.reset(m_srcL.width(), m_srcL.height());
            m_gpuL.setMemorySource(m_srcL);
        }
        else if (m_pathL.isEmpty() && m_gpuL.width() <= 0)
            m_gpuL.destroy();
        if (!m_srcR.isNull())
        {
            m_gpuR.reset(m_srcR.width(), m_srcR.height());
            m_gpuR.setMemorySource(m_srcR);
        }
        else if (m_pathR.isEmpty() && m_gpuR.width() <= 0)
            m_gpuR.destroy();
        m_gpuDirty = false;
    }

    const QRectF visL = visibleImageRect(Eye::Left);
    const QRectF visR = visibleImageRect(Eye::Right);
    const float zoom = float(m_camera.zoom());
    if (m_gpuL.width() > 0)
        m_gpuL.setView(visL, zoom);
    if (m_gpuR.width() > 0)
        m_gpuR.setView(visR, zoom);

    m_gpuL.pump(this, 2, 6);
    m_gpuR.pump(this, 2, 6);

    if (m_viewFetchPending && m_drag == DragNone)
    {
        syncViewTiles(0);
        syncViewTiles(1);
        m_viewFetchPending = false;
    }

    m_gpuStreaming = m_gpuL.needsWork() || m_gpuR.needsWork();
}

void StereoCanvas::syncViewTiles(int eye)
{
    const QString &path = (eye == 0) ? m_pathL : m_pathR;
    const quint64 job = (eye == 0) ? m_jobL : m_jobR;
    if (path.isEmpty() || job == 0 || !m_decoder)
        return;
    GpuImage *gpu = gpuForEye(eye);
    if (!gpu->needsFetch())
        return;

    const QRect want = gpu->wantedRect();
    const int level = gpu->currentLevel();
    int &gen = (eye == 0) ? m_fetchGenL : m_fetchGenR;
    int &fl = (eye == 0) ? m_fetchLevelL : m_fetchLevelR;
    QRect &fr = (eye == 0) ? m_fetchRectL : m_fetchRectR;

    // 正在读取的窗口已经盖住当前视窗时不要取消，否则大距离移屏后会反复作废解码。
    if (gen != 0 && fl == level && fr.adjusted(-64, -64, 64, 64).contains(want))
        return;

    gen = m_decoder->bumpWindow(eye);
    fl = level;
    fr = want;
    emit requestWindow(eye, job, gen, level, want.x(), want.y(), want.width(), want.height());
}

void StereoCanvas::onPatchReady(int eye, quint64 jobId, int gen, int x, int y, int w, int h, const QImage &image)
{
    if ((eye == 0 && jobId != m_jobL) || (eye == 1 && jobId != m_jobR) || image.isNull())
        return;
    const int curGen = (eye == 0) ? m_fetchGenL : m_fetchGenR;
    if (gen != curGen)
        return;
    makeCurrent();
    gpuForEye(eye)->setPatch(x, y, w, h, (eye == 0) ? m_fetchLevelL : m_fetchLevelR, image, this);
    doneCurrent();
    m_gpuStreaming = gpuForEye(eye)->needsWork();
    if (gpuForEye(eye)->needsFetch())
    {
        if (m_drag == DragNone)
            m_viewFetchPending = true;
        else
            noteViewChanged();
    }
    requestUpdate();
    emitStatus();
}

void StereoCanvas::onImageInfo(int eye, quint64 jobId, const QSize &fullSize, bool clipOk)
{
    Q_UNUSED(clipOk);
    if ((eye == 0 && jobId != m_jobL) || (eye == 1 && jobId != m_jobR))
        return;
    makeCurrent();
    beginGpuForEye(eye, fullSize.width(), fullSize.height());
    doneCurrent();
    applyImageSize(eye, fullSize.width(), fullSize.height(), true);
    m_loadHint = QString::fromUtf8("%1 已定位 %2x%3，按视窗加载金字塔...")
        .arg(eye == 0 ? m_nameL : m_nameR)
        .arg(fullSize.width())
        .arg(fullSize.height());
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus(true);
}

void StereoCanvas::onOverviewReady(int eye, quint64 jobId, const QImage &overview, const QSize &fullSize)
{
    if ((eye == 0 && jobId != m_jobL) || (eye == 1 && jobId != m_jobR) || overview.isNull())
        return;
    makeCurrent();
    GpuImage *gpu = gpuForEye(eye);
    if (gpu->width() != fullSize.width() || gpu->height() != fullSize.height())
        beginGpuForEye(eye, fullSize.width(), fullSize.height());
    gpu->setOverview(overview, this);
    doneCurrent();
    applyImageSize(eye, fullSize.width(), fullSize.height(), true);
    m_loadHint.clear();
    m_gpuStreaming = true;
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus(true);
}

void StereoCanvas::onDecodeFailed(int eye, quint64 jobId, const QString &message)
{
    if ((eye == 0 && jobId != m_jobL) || (eye == 1 && jobId != m_jobR))
        return;
    m_loadHint = QString::fromUtf8("打开失败: %1").arg(message);
    emitStatus();
}

void StereoCanvas::ensureFbos()
{
    const int w = std::max(1, int(std::ceil(width() * dpr())));
    const int h = std::max(1, int(std::ceil(height() * dpr())));
    if (!m_fboL.isNull() && m_fboL->width() == w && m_fboL->height() == h)
        return;

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    fmt.setSamples(0);
    m_fboL.reset(new QOpenGLFramebufferObject(w, h, fmt));
    m_fboR.reset(new QOpenGLFramebufferObject(w, h, fmt));
}

void StereoCanvas::renderEye(Eye eye)
{
    const int pw = std::max(1, int(std::ceil(width() * dpr())));
    const int ph = std::max(1, int(std::ceil(height() * dpr())));
    glViewport(0, 0, pw, ph);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    GpuImage *img = (eye == Eye::Left) ? &m_gpuL : &m_gpuR;
    const EyeAdjust &adj = (eye == Eye::Left) ? m_adjL : m_adjR;
    if (!img->isEmpty())
    {
        float mvp[9];
        // QMatrix3x3 构造函数按行主序读入；再转列主序上传。若先转成列主序会把平移丢掉。
        m_camera.imageToNdc(eye).toRowMajorFloat(mvp);
        img->draw(&m_progImage, this, mvp, adj, visibleImageRect(eye));
    }

    if (m_showCursor && m_mouseInside)
        drawCursor(eye);
}

void StereoCanvas::drawOverlayGl()
{
    if (!m_mouseInside)
        return;

    const Vec2 mouse = widgetToLogical(m_mousePos);
    const Vec2 imgL = m_camera.screenToImage(Eye::Left, mouse);
    const Vec2 rightMark = m_camera.imageToScreen(Eye::Right, imgL);
    const double w = std::max(1.0, m_camera.viewportWidth());
    const double h = std::max(1.0, m_camera.viewportHeight());
    auto toNdc = [&](const Vec2 &s, float *x, float *y) {
        *x = float(s.x / w * 2.0 - 1.0);
        *y = float(1.0 - s.y / h * 2.0);
    };
    float mx, my, rx, ry;
    toNdc(mouse, &mx, &my);
    toNdc(rightMark, &rx, &ry);

    const float ax = float(14.0 / w * 2.0);
    const float ay = float(14.0 / h * 2.0);
    const float gapx = float(4.0 / w * 2.0);
    const float gapy = float(4.0 / h * 2.0);
    const float mark[] = {
        rx - ax, ry, rx - gapx, ry,
        rx + gapx, ry, rx + ax, ry,
        rx, ry - ay, rx, ry - gapy,
        rx, ry + gapy, rx, ry + ay
    };
    const float link[] = { mx, my, rx, ry };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_progColor.bind();
    const int loc = m_progColor.attributeLocation("a_pos");
    glEnableVertexAttribArray(GLuint(loc));
    m_progColor.setUniformValue("u_color", QColor(80, 220, 255, 200));
    glLineWidth(1.5f);
    glVertexAttribPointer(GLuint(loc), 2, GL_FLOAT, GL_FALSE, 0, mark);
    glDrawArrays(GL_LINES, 0, 8);
    m_progColor.setUniformValue("u_color", QColor(80, 220, 255, 140));
    glVertexAttribPointer(GLuint(loc), 2, GL_FLOAT, GL_FALSE, 0, link);
    glDrawArrays(GL_LINES, 0, 2);
    glDisableVertexAttribArray(GLuint(loc));
    m_progColor.release();
    glLineWidth(1.0f);
}

void StereoCanvas::drawCursor(Eye /*eye*/)
{
    const Vec2 s = widgetToLogical(m_mousePos);
    const double w = std::max(1.0, m_camera.viewportWidth());
    const double h = std::max(1.0, m_camera.viewportHeight());
    const float nx = float(s.x / w * 2.0 - 1.0);
    const float ny = float(1.0 - s.y / h * 2.0);
    const float ax = float(18.0 / w * 2.0);
    const float ay = float(18.0 / h * 2.0);
    const float gapx = float(5.0 / w * 2.0);
    const float gapy = float(5.0 / h * 2.0);

    m_progColor.bind();
    const int loc = m_progColor.attributeLocation("a_pos");
    glEnableVertexAttribArray(GLuint(loc));

    auto drawLines = [&](const float *pts, int n, const QColor &c, float width)
    {
        m_progColor.setUniformValue("u_color", c);
        glLineWidth(width);
        glVertexAttribPointer(GLuint(loc), 2, GL_FLOAT, GL_FALSE, 0, pts);
        glDrawArrays(GL_LINES, 0, n);
    };

    const float cross[] = {
        nx - ax, ny, nx - gapx, ny,
        nx + gapx, ny, nx + ax, ny,
        nx, ny - ay, nx, ny - gapy,
        nx, ny + gapy, nx, ny + ay
    };
    drawLines(cross, 8, QColor(255, 230, 80), 2.0f);

    const int seg = 48;
    if (m_cursorCircle.size() != seg * 4)
        m_cursorCircle.resize(seg * 4);
    const float rx = float(42.0 / w * 2.0);
    const float ry = float(42.0 / h * 2.0);
    for (int i = 0; i < seg; ++i)
    {
        const float a0 = float(i) / float(seg) * float(M_PI * 2.0);
        const float a1 = float(i + 1) / float(seg) * float(M_PI * 2.0);
        m_cursorCircle[i * 4 + 0] = nx + std::cos(a0) * rx;
        m_cursorCircle[i * 4 + 1] = ny + std::sin(a0) * ry;
        m_cursorCircle[i * 4 + 2] = nx + std::cos(a1) * rx;
        m_cursorCircle[i * 4 + 3] = ny + std::sin(a1) * ry;
    }
    drawLines(m_cursorCircle.constData(), seg * 2, QColor(80, 220, 255, 200), 1.0f);

    glDisableVertexAttribArray(GLuint(loc));
    m_progColor.release();
    glLineWidth(1.0f);
}

void StereoCanvas::composite()
{
    GLuint texL = m_fboL->texture();
    GLuint texR = m_fboR->texture();
    if (m_camera.inverseStereo())
        std::swap(texL, texR);

    m_progComposite.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texL);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texR);
    m_progComposite.setUniformValue("u_left", 0);
    m_progComposite.setUniformValue("u_right", 1);
    m_progComposite.setUniformValue("u_mode", compositeShaderMode());
    m_progComposite.setUniformValue("u_fbSize", QVector2D(float(width()), float(height())));
    m_progComposite.setUniformValue("u_swapInterlace", m_swapInterlace ? 1 : 0);

    const float quad[] = {
        -1.f, -1.f,
         1.f, -1.f,
         1.f,  1.f,
        -1.f,  1.f
    };
    const int loc = m_progComposite.attributeLocation("a_pos");
    glEnableVertexAttribArray(GLuint(loc));
    glVertexAttribPointer(GLuint(loc), 2, GL_FLOAT, GL_FALSE, 0, quad);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableVertexAttribArray(GLuint(loc));
    m_progComposite.release();
    unbindStereoTextures();
}

Vec2 StereoCanvas::widgetToLogical(const QPoint &p) const
{
    return Vec2(p.x(), p.y());
}

QImage StereoCanvas::loadImageFile(const QString &path) const
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull())
        img = QImage(path);
    return img;
}

void StereoCanvas::emitStatus(bool force)
{
    if (!force && m_statusTimer.isValid() && m_statusTimer.elapsed() < 32)
        return;
    m_statusTimer.restart();
    const Vec2 imgL = m_camera.screenToImage(Eye::Left, widgetToLogical(m_mousePos));
    const Vec2 imgR = m_camera.screenToImage(Eye::Right, widgetToLogical(m_mousePos));
    QString stereoHint;
    if (m_mode == DisplayMode::Shutter)
        stereoHint = m_hasHwStereo
            ? QString::fromUtf8(" | 硬件四缓冲")
            : QString::fromUtf8(" | 软件高速翻页");
    const Vec2 shift = m_camera.rightImageScreenShift();
    QString fillHint;
    if (m_gpuL.width() > 0)
        fillHint += QString::fromUtf8(" | 左 %1").arg(m_gpuL.info());
    if (m_gpuR.width() > 0)
        fillHint += QString::fromUtf8(" | 右 %1").arg(m_gpuR.info());
    if (!m_loadHint.isEmpty())
        fillHint += QString::fromUtf8(" | ") + m_loadHint;
    const QString text = QString::fromUtf8(
        "模式 %1 | 缩放 %2 | 视差 %3 px | 右片 ΔX %4 ΔY %5 | 左(%6, %7) 右(%8, %9) | %10 / %11%12%13")
        .arg(QString::fromLatin1(displayModeKey(m_mode)))
        .arg(m_camera.zoom(), 0, 'f', 3)
        .arg(m_camera.parallax(), 0, 'f', 1)
        .arg(shift.x - m_camera.parallax(), 0, 'f', 1)
        .arg(shift.y, 0, 'f', 1)
        .arg(imgL.x, 0, 'f', 1)
        .arg(imgL.y, 0, 'f', 1)
        .arg(imgR.x, 0, 'f', 1)
        .arg(imgR.y, 0, 'f', 1)
        .arg(m_nameL.isEmpty() ? QStringLiteral("-") : m_nameL)
        .arg(m_nameR.isEmpty() ? QStringLiteral("-") : m_nameR)
        .arg(stereoHint)
        .arg(fillHint);
    emit statusChanged(text);
}

void StereoCanvas::mousePressEvent(QMouseEvent *e)
{
    requestActivate();
    m_lastPos = e->pos();
    m_mousePos = e->pos();
    m_mouseInside = true;
    if (e->button() == Qt::RightButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier)))
        m_drag = DragAlign;
    else if (e->button() == Qt::LeftButton || (e->button() == Qt::MiddleButton))
        m_drag = DragPan;
    emitStatus(true);
}

void StereoCanvas::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint d = e->pos() - m_lastPos;
    m_mousePos = e->pos();
    m_mouseInside = true;
    m_lastPos = e->pos();
    if (m_drag == DragPan)
    {
        m_camera.panScreen(d.x(), d.y());
        noteViewChanged();
        requestUpdate();
        emit viewChanged();
    }
    else if (m_drag == DragAlign)
    {
        m_camera.alignRightScreen(d.x(), d.y());
        noteViewChanged();
        requestUpdate();
        emit viewChanged();
    }
    else
    {
        // 仅十字丝跟随，不作为影像刷新条件。
        requestUpdate();
    }
    emitStatus();
}

void StereoCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    Q_UNUSED(e);
    const bool viewDrag = (m_drag == DragPan || m_drag == DragAlign);
    m_drag = DragNone;
    if (viewDrag)
        noteViewChanged();
    requestUpdate();
    emitStatus(true);
}

void StereoCanvas::wheelEvent(QWheelEvent *e)
{
    requestActivate();
    m_mousePos = e->pos();
    m_mouseInside = true;

    double stepsY = e->angleDelta().y() / 120.0;
    double stepsX = e->angleDelta().x() / 120.0;
    if (qFuzzyIsNull(stepsY) && qFuzzyIsNull(stepsX) && !e->pixelDelta().isNull())
    {
        stepsY = e->pixelDelta().y() / 40.0;
        stepsX = e->pixelDelta().x() / 40.0;
    }

    const double stepPx = 16.0;
    const bool shift = (e->modifiers() & Qt::ShiftModifier) != 0;
    const bool alt = (e->modifiers() & Qt::AltModifier) != 0;
    const bool horizontalWheel = (e->orientation() == Qt::Horizontal) ||
        (qFuzzyIsNull(stepsY) && !qFuzzyIsNull(stepsX));

    if (e->modifiers() & Qt::ControlModifier)
    {
        const double steps = !qFuzzyIsNull(stepsY) ? stepsY : stepsX;
        if (qFuzzyIsNull(steps))
            return;
        const double factor = std::pow(1.12, steps);
        m_camera.zoomAtScreen(Eye::Left, widgetToLogical(e->pos()), factor);
    }
    else if (alt)
    {
        const double steps = !qFuzzyIsNull(stepsY) ? stepsY : stepsX;
        if (qFuzzyIsNull(steps))
            return;
        m_camera.alignRightScreen(0.0, -steps * stepPx);
    }
    else if (shift || horizontalWheel)
    {
        // Windows 上 Shift+滚轮常变成横向滚轮，甚至不再带 Shift。
        const double steps = !qFuzzyIsNull(stepsX) ? stepsX : stepsY;
        if (qFuzzyIsNull(steps))
            return;
        m_camera.alignRightScreen(steps * stepPx, 0.0);
    }
    else if (!qFuzzyIsNull(stepsY))
    {
        m_camera.addParallax(stepsY * stepPx);
    }
    else
    {
        return;
    }
    e->accept();
    noteViewChanged();
    requestUpdate();
    emit viewChanged();
    emitStatus();
}

void StereoCanvas::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt)
    {
        requestUpdate();
        return;
    }
    switch (e->key())
    {
    case Qt::Key_BracketLeft:
    case Qt::Key_Minus:
        addParallax(-2.0);
        break;
    case Qt::Key_BracketRight:
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        addParallax(2.0);
        break;
    case Qt::Key_Up:
        if (e->modifiers() & (Qt::ShiftModifier | Qt::AltModifier))
        {
            QOpenGLWindow::keyPressEvent(e);
            return;
        }
        addParallax(4.0);
        break;
    case Qt::Key_Down:
        if (e->modifiers() & (Qt::ShiftModifier | Qt::AltModifier))
        {
            QOpenGLWindow::keyPressEvent(e);
            return;
        }
        addParallax(-4.0);
        break;
    case Qt::Key_Left:
        if (e->modifiers() & Qt::ShiftModifier)
        {
            QOpenGLWindow::keyPressEvent(e);
            return;
        }
        m_camera.panScreen(-24.0, 0.0);
        noteViewChanged();
        requestUpdate();
        emit viewChanged();
        emitStatus();
        break;
    case Qt::Key_Right:
        if (e->modifiers() & Qt::ShiftModifier)
        {
            QOpenGLWindow::keyPressEvent(e);
            return;
        }
        m_camera.panScreen(24.0, 0.0);
        noteViewChanged();
        requestUpdate();
        emit viewChanged();
        emitStatus();
        break;
    case Qt::Key_I:
        setInverseStereo(!m_camera.inverseStereo());
        break;
    case Qt::Key_F:
        fitView();
        break;
    case Qt::Key_Home:
        resetView();
        break;
    default:
        QOpenGLWindow::keyPressEvent(e);
        break;
    }
}

void StereoCanvas::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt)
        requestUpdate();
    QOpenGLWindow::keyReleaseEvent(e);
}

bool StereoCanvas::event(QEvent *e)
{
    if (e->type() == QEvent::Leave)
    {
        m_mouseInside = false;
        requestUpdate();
    }
    else if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove)
    {
        QDropEvent *de = static_cast<QDropEvent *>(e);
        if (de->mimeData() && de->mimeData()->hasUrls())
        {
            de->acceptProposedAction();
            return true;
        }
    }
    else if (e->type() == QEvent::Drop)
    {
        QDropEvent *de = static_cast<QDropEvent *>(e);
        if (de->mimeData() && handleDropUrls(de->mimeData()->urls()))
        {
            de->acceptProposedAction();
            return true;
        }
    }
    return QOpenGLWindow::event(e);
}

bool StereoCanvas::handleDropUrls(const QList<QUrl> &urls)
{
    QStringList files;
    for (int i = 0; i < urls.size(); ++i)
    {
        if (urls[i].isLocalFile())
            files.push_back(urls[i].toLocalFile());
    }
    if (files.size() >= 2)
    {
        loadLeft(files[0]);
        loadRight(files[1]);
        m_camera.fitBoth();
        requestUpdate();
        return true;
    }
    if (files.size() == 1)
    {
        if (m_srcL.isNull())
            loadLeft(files[0]);
        else if (m_srcR.isNull())
            loadRight(files[0]);
        else
            loadLeft(files[0]);
        requestUpdate();
        return true;
    }
    return false;
}

} // namespace qsv
