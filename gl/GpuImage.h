#pragma once

#include <QImage>
#include <QVector>
#include <QString>
#include <QRect>
#include <QRectF>
#include <QHash>
#include <QSet>
#include <QOpenGLShaderProgram>
#include <QOpenGLFunctions>

namespace qsv
{

struct EyeAdjust
{
    float brightness;
    float contrast;
    float gamma;

    EyeAdjust()
        : brightness(0.0f)
        , contrast(1.0f)
        , gamma(1.0f)
    {
    }
};

struct TileId
{
    int level;
    int col;
    int row;

    TileId()
        : level(0)
        , col(0)
        , row(0)
    {
    }

    TileId(int l, int c, int r)
        : level(l)
        , col(c)
        , row(r)
    {
    }

    bool operator==(const TileId &o) const
    {
        return level == o.level && col == o.col && row == o.row;
    }
};

inline uint qHash(const TileId &k)
{
    return uint(k.level * 73856093) ^ uint(k.col * 19349663) ^ uint(k.row * 83492791);
}

// 视窗 + 金字塔缓存：只保留视图覆盖范围和外扩瓦片，并限制驻留数量。
class GpuImage
{
public:
    static int tilePixels();
    static int tileExtent(int level);

    GpuImage();
    ~GpuImage();

    void destroy();
    void reset(int width, int height);
    void setOverview(const QImage &preview, QOpenGLFunctions *gl);
    void setMemorySource(const QImage &image);
    void setPatch(int x, int y, int w, int h, int level, const QImage &image, QOpenGLFunctions *gl);

    void setView(const QRectF &visible, float zoom);
    int currentLevel() const { return m_level; }
    int maxLevel() const;
    QRect wantedRect() const { return m_wantedRect; }
    bool needsFetch() const;
    QVector<TileId> missingWanted() const;

    bool addTile(int level, int x, int y, const QImage &piece);
    int pump(QOpenGLFunctions *gl, int maxTiles, int maxMs);

    bool isEmpty() const;
    bool needsWork() const;
    int residentCount() const;
    int wantedCount() const { return m_wanted.size(); }
    int width() const { return m_width; }
    int height() const { return m_height; }
    QString info() const;

    void draw(QOpenGLShaderProgram *program,
              QOpenGLFunctions *gl,
              const float mvpRowMajor[9],
              const EyeAdjust &adj,
              const QRectF &visible) const;

private:
    struct Tile
    {
        TileId id;
        int x;
        int y;
        int w;
        int h;
        GLuint texId;
        QImage pending;
        quint64 used;

        Tile()
            : x(0)
            , y(0)
            , w(0)
            , h(0)
            , texId(0)
            , used(0)
        {
        }
    };

    static bool uploadTexture(GLuint *texId, const QImage &piece, QOpenGLFunctions *gl);
    int levelForZoom(float zoom) const;
    TileId idFromOrigin(int level, int x, int y) const;
    QRect tileImageRect(const TileId &id) const;
    void fillFromMemory();
    void evict(QOpenGLFunctions *gl);

    int m_width;
    int m_height;
    int m_level;
    int m_maxResident;
    quint64 m_stamp;
    QRect m_wantedRect;
    QImage m_source;
    GLuint m_overviewTex;
    int m_overviewW;
    int m_overviewH;
    GLuint m_patchTex;
    int m_patchLevel;
    int m_patchX;
    int m_patchY;
    int m_patchW;
    int m_patchH;
    QSet<TileId> m_wanted;
    QHash<TileId, Tile> m_tiles;
};

} // namespace qsv
