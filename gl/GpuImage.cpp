#include "GpuImage.h"

#include <QOpenGLContext>
#include <QMatrix3x3>
#include <QVector4D>
#include <QElapsedTimer>
#include <QByteArray>
#include <QList>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace qsv
{

static const int kTilePixels = 512;
static const int kMaxResident = 36;
static const int kMaxLevel = 12;

static QImage toRgba(const QImage &src)
{
    if (src.isNull())
        return src;
    if (src.format() == QImage::Format_RGBA8888)
        return src;
    return src.convertToFormat(QImage::Format_RGBA8888);
}

int GpuImage::tilePixels()
{
    return kTilePixels;
}

int GpuImage::tileExtent(int level)
{
    level = std::max(0, std::min(kMaxLevel, level));
    return kTilePixels << level;
}

GpuImage::GpuImage()
    : m_width(0)
    , m_height(0)
    , m_level(0)
    , m_maxResident(kMaxResident)
    , m_stamp(1)
    , m_overviewTex(0)
    , m_overviewW(0)
    , m_overviewH(0)
    , m_patchTex(0)
    , m_patchLevel(-1)
    , m_patchX(0)
    , m_patchY(0)
    , m_patchW(0)
    , m_patchH(0)
{
}

GpuImage::~GpuImage()
{
    destroy();
}

void GpuImage::destroy()
{
    QOpenGLFunctions *gl = 0;
    if (QOpenGLContext::currentContext())
        gl = QOpenGLContext::currentContext()->functions();
    if (gl)
    {
        if (m_overviewTex)
            gl->glDeleteTextures(1, &m_overviewTex);
        if (m_patchTex)
            gl->glDeleteTextures(1, &m_patchTex);
        QHash<TileId, Tile>::iterator it = m_tiles.begin();
        for (; it != m_tiles.end(); ++it)
        {
            if (it->texId)
                gl->glDeleteTextures(1, &it->texId);
        }
    }
    m_overviewTex = 0;
    m_overviewW = 0;
    m_overviewH = 0;
    m_patchTex = 0;
    m_patchLevel = -1;
    m_patchX = 0;
    m_patchY = 0;
    m_patchW = 0;
    m_patchH = 0;
    m_tiles.clear();
    m_wanted.clear();
    m_source = QImage();
    m_wantedRect = QRect();
    m_width = 0;
    m_height = 0;
    m_level = 0;
}

void GpuImage::reset(int width, int height)
{
    destroy();
    m_width = std::max(0, width);
    m_height = std::max(0, height);
    m_level = maxLevel();
}

int GpuImage::maxLevel() const
{
    if (m_width <= 0 || m_height <= 0)
        return 0;
    const int longest = std::max(m_width, m_height);
    int level = 0;
    while (level < kMaxLevel && tileExtent(level) < longest)
        ++level;
    return level;
}

int GpuImage::levelForZoom(float zoom) const
{
    const int maxL = maxLevel();
    if (zoom <= 1e-6f)
        return maxL;
    int level = 0;
    while (level < maxL && zoom * float(1 << level) < 0.85f)
        ++level;
    return level;
}

TileId GpuImage::idFromOrigin(int level, int x, int y) const
{
    const int ext = std::max(1, tileExtent(level));
    return TileId(level, x / ext, y / ext);
}

QRect GpuImage::tileImageRect(const TileId &id) const
{
    const int ext = tileExtent(id.level);
    const int x = id.col * ext;
    const int y = id.row * ext;
    const int w = std::max(1, std::min(ext, m_width - x));
    const int h = std::max(1, std::min(ext, m_height - y));
    return QRect(x, y, w, h);
}

void GpuImage::setMemorySource(const QImage &image)
{
    m_source = image;
    if (m_width <= 0 || m_height <= 0)
    {
        m_width = image.width();
        m_height = image.height();
    }
}

void GpuImage::setOverview(const QImage &preview, QOpenGLFunctions *gl)
{
    if (!gl || preview.isNull())
        return;
    m_overviewW = preview.width();
    m_overviewH = preview.height();
    uploadTexture(&m_overviewTex, preview, gl);
}

void GpuImage::setPatch(int x, int y, int w, int h, int level, const QImage &image, QOpenGLFunctions *gl)
{
    if (!gl || image.isNull() || w <= 0 || h <= 0)
        return;
    m_patchX = x;
    m_patchY = y;
    m_patchW = w;
    m_patchH = h;
    m_patchLevel = std::max(0, level);
    uploadTexture(&m_patchTex, image, gl);
}

bool GpuImage::needsFetch() const
{
    if (m_width <= 0 || m_wantedRect.isEmpty())
        return false;
    if (!m_source.isNull())
        return !missingWanted().isEmpty();
    if (m_patchTex && m_patchW > 0 && m_patchH > 0)
    {
        // 总览级大块在影像坐标上会盖住任何子窗口，但分辨率不够，缩放/移屏后必须重取。
        if (m_patchLevel < 0 || m_patchLevel > m_level)
            return true;
        const QRect patch(m_patchX, m_patchY, m_patchW, m_patchH);
        QRect need = m_wantedRect.adjusted(8, 8, -8, -8);
        if (need.width() <= 0 || need.height() <= 0)
            need = m_wantedRect;
        if (patch.contains(need))
            return false;
    }
    return true;
}

void GpuImage::setView(const QRectF &visible, float zoom)
{
    if (m_width <= 0 || m_height <= 0)
        return;

    m_level = levelForZoom(zoom);
    const int ext = tileExtent(m_level);
    QRect vis = visible.toAlignedRect();
    vis.adjust(-ext, -ext, ext, ext);
    vis = vis.intersected(QRect(0, 0, m_width, m_height));
    if (!vis.isValid())
        vis = QRect(0, 0, m_width, m_height);
    m_wantedRect = vis;
    m_wanted.clear();
    ++m_stamp;

    const int x0 = std::max(0, (vis.left() / ext) * ext);
    const int y0 = std::max(0, (vis.top() / ext) * ext);
    for (int y = y0; y < vis.bottom() && y < m_height; y += ext)
    {
        for (int x = x0; x < vis.right() && x < m_width; x += ext)
        {
            const TileId id = idFromOrigin(m_level, x, y);
            m_wanted.insert(id);
            QHash<TileId, Tile>::iterator it = m_tiles.find(id);
            if (it != m_tiles.end())
                it->used = m_stamp;
        }
    }

    fillFromMemory();
}

void GpuImage::fillFromMemory()
{
    if (m_source.isNull())
        return;
    QSet<TileId>::const_iterator it = m_wanted.constBegin();
    for (; it != m_wanted.constEnd(); ++it)
    {
        const TileId id = *it;
        QHash<TileId, Tile>::iterator have = m_tiles.find(id);
        if (have != m_tiles.end() && (have->texId || !have->pending.isNull()))
            continue;
        const QRect r = tileImageRect(id);
        const QImage piece = m_source.copy(r);
        if (piece.isNull())
            continue;
        Tile t;
        t.id = id;
        t.x = r.x();
        t.y = r.y();
        t.w = r.width();
        t.h = r.height();
        t.texId = 0;
        t.pending = piece;
        t.used = m_stamp;
        m_tiles.insert(id, t);
    }
}

QVector<TileId> GpuImage::missingWanted() const
{
    QVector<TileId> miss;
    QSet<TileId>::const_iterator it = m_wanted.constBegin();
    for (; it != m_wanted.constEnd(); ++it)
    {
        const TileId id = *it;
        QHash<TileId, Tile>::const_iterator have = m_tiles.constFind(id);
        if (have != m_tiles.constEnd() && (have->texId || !have->pending.isNull()))
            continue;
        miss.push_back(id);
    }
    return miss;
}

bool GpuImage::addTile(int level, int x, int y, const QImage &piece)
{
    if (piece.isNull() || m_width <= 0)
        return false;
    const TileId id = idFromOrigin(level, x, y);
    const QRect r = tileImageRect(id);
    Tile t = m_tiles.value(id);
    t.id = id;
    t.x = r.x();
    t.y = r.y();
    t.w = r.width();
    t.h = r.height();
    t.pending = piece;
    t.used = m_stamp;
    m_tiles.insert(id, t);
    return true;
}

bool GpuImage::isEmpty() const
{
    if (m_overviewTex || m_patchTex)
        return false;
    QHash<TileId, Tile>::const_iterator it = m_tiles.constBegin();
    for (; it != m_tiles.constEnd(); ++it)
    {
        if (it->texId)
            return false;
    }
    return true;
}

bool GpuImage::needsWork() const
{
    QHash<TileId, Tile>::const_iterator it = m_tiles.constBegin();
    for (; it != m_tiles.constEnd(); ++it)
    {
        if (!it->pending.isNull())
            return true;
    }
    return false;
}

int GpuImage::residentCount() const
{
    int n = 0;
    QHash<TileId, Tile>::const_iterator it = m_tiles.constBegin();
    for (; it != m_tiles.constEnd(); ++it)
    {
        if (it->texId)
            ++n;
    }
    return n;
}

QString GpuImage::info() const
{
    if (m_width <= 0)
        return QString();
    return QString::fromUtf8("%1x%2 金字塔L%3/%4 视窗%5/%6")
        .arg(m_width)
        .arg(m_height)
        .arg(m_level)
        .arg(maxLevel())
        .arg(residentCount())
        .arg(std::max(1, m_wanted.size()));
}

bool GpuImage::uploadTexture(GLuint *texId, const QImage &piece, QOpenGLFunctions *gl)
{
    if (!texId || !gl || piece.isNull())
        return false;

    const QImage rgba = toRgba(piece);
    const int tw = rgba.width();
    const int th = rgba.height();
    if (tw <= 0 || th <= 0)
        return false;

    QByteArray packed;
    packed.resize(tw * th * 4);
    const int rowBytes = tw * 4;
    for (int row = 0; row < th; ++row)
        memcpy(packed.data() + row * rowBytes, rgba.constScanLine(row), size_t(std::min(rowBytes, rgba.bytesPerLine())));

    if (*texId)
        gl->glDeleteTextures(1, texId);
    *texId = 0;
    gl->glGenTextures(1, texId);
    gl->glBindTexture(GL_TEXTURE_2D, *texId);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, packed.constData());
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return *texId != 0;
}

void GpuImage::evict(QOpenGLFunctions *gl)
{
    if (!gl)
        return;
    while (m_tiles.size() > m_maxResident)
    {
        TileId victim;
        quint64 oldest = ~quint64(0);
        bool found = false;
        QHash<TileId, Tile>::iterator it = m_tiles.begin();
        for (; it != m_tiles.end(); ++it)
        {
            if (m_wanted.contains(it.key()))
                continue;
            if (it->used <= oldest)
            {
                oldest = it->used;
                victim = it.key();
                found = true;
            }
        }
        if (!found)
            break;
        Tile &t = m_tiles[victim];
        if (t.texId)
            gl->glDeleteTextures(1, &t.texId);
        m_tiles.remove(victim);
    }
}

int GpuImage::pump(QOpenGLFunctions *gl, int maxTiles, int maxMs)
{
    if (!gl)
        return 0;
    QElapsedTimer timer;
    timer.start();
    int done = 0;

    QList<TileId> pendingIds;
    QHash<TileId, Tile>::iterator it = m_tiles.begin();
    for (; it != m_tiles.end(); ++it)
    {
        if (!it->pending.isNull())
            pendingIds.push_back(it.key());
    }

    for (int i = 0; i < pendingIds.size(); ++i)
    {
        if (maxTiles > 0 && done >= maxTiles)
            break;
        if (maxMs > 0 && timer.elapsed() >= maxMs)
            break;
        const TileId id = pendingIds[i];
        QHash<TileId, Tile>::iterator tile = m_tiles.find(id);
        if (tile == m_tiles.end() || tile->pending.isNull())
            continue;
        const bool prefer = m_wanted.contains(id);
        if (!prefer && done > 0)
            continue;
        if (!uploadTexture(&tile->texId, tile->pending, gl))
            break;
        tile->pending = QImage();
        ++done;
    }

    evict(gl);
    return done;
}

void GpuImage::draw(QOpenGLShaderProgram *program,
                    QOpenGLFunctions *gl,
                    const float mvpRowMajor[9],
                    const EyeAdjust &adj,
                    const QRectF &visible) const
{
    if (!program || !gl || isEmpty())
        return;

    program->bind();
    program->setUniformValue("u_mvp", QMatrix3x3(mvpRowMajor));
    program->setUniformValue("u_tex", 0);
    program->setUniformValue("u_brightness", adj.brightness);
    program->setUniformValue("u_contrast", adj.contrast);
    program->setUniformValue("u_gamma", adj.gamma);

    const int locPos = program->attributeLocation("a_pos");
    const float unit[8] = {
        0.f, 0.f,
        1.f, 0.f,
        1.f, 1.f,
        0.f, 1.f
    };

    gl->glEnableVertexAttribArray(GLuint(locPos));
    gl->glVertexAttribPointer(GLuint(locPos), 2, GL_FLOAT, GL_FALSE, 0, unit);
    gl->glActiveTexture(GL_TEXTURE0);

    auto drawRect = [&](GLuint tex, float x, float y, float w, float h) {
        if (!tex)
            return;
        program->setUniformValue("u_rect", QVector4D(x, y, w, h));
        gl->glBindTexture(GL_TEXTURE_2D, tex);
        gl->glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    };

    if (m_overviewTex)
        drawRect(m_overviewTex, 0.f, 0.f, float(m_width), float(m_height));
    if (m_patchTex)
        drawRect(m_patchTex, float(m_patchX), float(m_patchY), float(m_patchW), float(m_patchH));

    QVector<const Tile *> order;
    QHash<TileId, Tile>::const_iterator it = m_tiles.constBegin();
    for (; it != m_tiles.constEnd(); ++it)
    {
        if (!it->texId)
            continue;
        if (visible.width() > 1.0 && visible.height() > 1.0
            && !visible.intersects(QRectF(it->x, it->y, it->w, it->h)))
            continue;
        order.push_back(&it.value());
    }
    std::sort(order.begin(), order.end(), [](const Tile *a, const Tile *b) {
        return a->id.level > b->id.level;
    });
    for (int i = 0; i < order.size(); ++i)
        drawRect(order[i]->texId, float(order[i]->x), float(order[i]->y),
                 float(order[i]->w), float(order[i]->h));

    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glDisableVertexAttribArray(GLuint(locPos));
    program->release();
}

} // namespace qsv
