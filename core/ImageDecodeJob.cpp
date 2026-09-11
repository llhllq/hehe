#include "ImageDecodeJob.h"
#include "GpuImage.h"

#include <QImageReader>
#include <QFileInfo>
#include <QRect>
#include <algorithm>

namespace qsv
{

static const int kOverviewMax = 2048;
static const int kRasterCap = 4096;
static const int kPatchCap = 2560;

ImageDecodeJob::ImageDecodeJob(QObject *parent)
    : QObject(parent)
    , m_cancel(0)
{
    m_clipOk[0] = false;
    m_clipOk[1] = false;
}

void ImageDecodeJob::requestCancel()
{
    m_cancel.storeRelease(1);
    bumpWindow(0);
    bumpWindow(1);
}

int ImageDecodeJob::bumpWindow(int eye)
{
    const int i = eye ? 1 : 0;
    return m_winGen[i].fetchAndAddOrdered(1) + 1;
}

bool ImageDecodeJob::cancelled() const
{
    return m_cancel.loadAcquire() != 0;
}

bool ImageDecodeJob::clipSupported(const QString &path, const QSize &full) const
{
    if (full.width() < 64 || full.height() < 64)
        return false;
    QImageReader r(path);
    r.setAutoTransform(false);
    r.setClipRect(QRect(0, 0, 32, 32));
    const QImage img = r.read();
    if (img.isNull())
        return false;
    if (img.width() >= full.width() && img.height() >= full.height())
        return false;
    return img.width() <= 64 && img.height() <= 64;
}

QImage ImageDecodeJob::readOverview(const QString &path, const QSize &full) const
{
    QSize preview = full;
    preview.scale(kOverviewMax, kOverviewMax, Qt::KeepAspectRatio);
    QImageReader r(path);
    r.setAutoTransform(false);
    if (preview.width() < full.width() || preview.height() < full.height())
        r.setScaledSize(preview);
    return r.read();
}

QImage ImageDecodeJob::readClipRect(const QString &path, const QSize &full, const QRect &src, int level) const
{
    QRect clip = src.intersected(QRect(0, 0, full.width(), full.height()));
    if (clip.width() <= 0 || clip.height() <= 0)
        return QImage();
    const int step = 1 << std::max(0, level);
    int outW = std::max(1, (clip.width() + step - 1) / step);
    int outH = std::max(1, (clip.height() + step - 1) / step);
    if (outW > kPatchCap || outH > kPatchCap)
    {
        QSize s(outW, outH);
        s.scale(kPatchCap, kPatchCap, Qt::KeepAspectRatio);
        outW = s.width();
        outH = s.height();
    }
    QImageReader r(path);
    r.setAutoTransform(false);
    r.setClipRect(clip);
    r.setScaledSize(QSize(outW, outH));
    QImage img = r.read();
    if (img.isNull())
        return img;
    if (img.width() >= full.width() && img.height() >= full.height()
        && (clip.width() < full.width() || clip.height() < full.height()))
        return QImage();
    return img;
}

QImage ImageDecodeJob::readRasterPatch(int eye, const QRect &src, int level)
{
    const QSize full = m_full[eye];
    if (full.width() <= 0 || m_path[eye].isEmpty())
        return QImage();
    const int step = 1 << std::max(0, level);
    int needW = std::max(1, (full.width() + step - 1) / step);
    int needH = std::max(1, (full.height() + step - 1) / step);
    if (needW > kRasterCap || needH > kRasterCap)
    {
        QSize s(full.width(), full.height());
        s.scale(kRasterCap, kRasterCap, Qt::KeepAspectRatio);
        needW = s.width();
        needH = s.height();
    }
    if (m_raster[eye].isNull()
        || m_raster[eye].width() < needW
        || m_raster[eye].height() < needH)
    {
        QImageReader r(m_path[eye]);
        r.setAutoTransform(false);
        r.setScaledSize(QSize(needW, needH));
        m_raster[eye] = r.read();
    }
    const QImage &raster = m_raster[eye];
    if (raster.isNull())
        return QImage();
    QRect clip = src.intersected(QRect(0, 0, full.width(), full.height()));
    const double sx = double(raster.width()) / double(full.width());
    const double sy = double(raster.height()) / double(full.height());
    const int rx = std::max(0, int(clip.x() * sx));
    const int ry = std::max(0, int(clip.y() * sy));
    const int rw = std::max(1, int(clip.width() * sx));
    const int rh = std::max(1, int(clip.height() * sy));
    return raster.copy(rx, ry, std::min(rw, raster.width() - rx), std::min(rh, raster.height() - ry));
}

void ImageDecodeJob::openFile(const QString &path, int eye, quint64 jobId)
{
    m_cancel.storeRelease(0);
    const int i = eye ? 1 : 0;
    bumpWindow(i);
    m_path[i] = path;
    m_full[i] = QSize();
    m_clipOk[i] = false;
    m_raster[i] = QImage();

    if (!QFileInfo::exists(path))
    {
        emit failed(eye, jobId, QString::fromUtf8("文件不存在"));
        return;
    }

    QImageReader probe(path);
    probe.setAutoTransform(false);
    QSize full = probe.size();
    if (full.width() <= 0 || full.height() <= 0)
    {
        QImage img = probe.read();
        if (img.isNull())
            img = QImage(path);
        if (img.isNull())
        {
            emit failed(eye, jobId, probe.errorString());
            return;
        }
        full = img.size();
        m_full[i] = full;
        m_clipOk[i] = false;
        m_raster[i] = img;
        emit imageInfo(eye, jobId, full, false);
        emit overviewReady(eye, jobId, img, full);
        return;
    }

    m_full[i] = full;
    m_clipOk[i] = clipSupported(path, full);
    emit imageInfo(eye, jobId, full, m_clipOk[i]);
    if (cancelled())
        return;
    const QImage overview = readOverview(path, full);
    if (overview.isNull())
    {
        emit failed(eye, jobId, QString::fromUtf8("无法读取影像预览"));
        return;
    }
    emit overviewReady(eye, jobId, overview, full);
}

void ImageDecodeJob::fetchWindow(int eye, quint64 jobId, int gen, int level, int x, int y, int w, int h)
{
    const int i = eye ? 1 : 0;
    if (m_winGen[i].loadAcquire() != gen)
        return;
    if (cancelled() || m_path[i].isEmpty() || m_full[i].isEmpty())
        return;

    QRect src(x, y, w, h);
    src = src.intersected(QRect(0, 0, m_full[i].width(), m_full[i].height()));
    if (src.width() <= 0 || src.height() <= 0)
        return;

    QImage sheet;
    if (m_clipOk[i])
        sheet = readClipRect(m_path[i], m_full[i], src, level);
    if (sheet.isNull())
        sheet = readRasterPatch(i, src, level);
    if (sheet.isNull())
        return;
    if (m_winGen[i].loadAcquire() != gen || cancelled())
        return;
    emit patchReady(eye, jobId, gen, src.x(), src.y(), src.width(), src.height(), sheet);
}

} // namespace qsv
