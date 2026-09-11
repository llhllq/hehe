#pragma once

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QAtomicInt>

namespace qsv
{

// 按需解码：打开时只出总览；移屏后一次读取当前视窗矩形，避免逐块反复打开文件。
class ImageDecodeJob : public QObject
{
    Q_OBJECT
public:
    explicit ImageDecodeJob(QObject *parent = 0);

    void requestCancel();
    int bumpWindow(int eye);

public slots:
    void openFile(const QString &path, int eye, quint64 jobId);
    void fetchWindow(int eye, quint64 jobId, int gen, int level, int x, int y, int w, int h);

signals:
    void imageInfo(int eye, quint64 jobId, const QSize &fullSize, bool clipOk);
    void overviewReady(int eye, quint64 jobId, const QImage &overview, const QSize &fullSize);
    void patchReady(int eye, quint64 jobId, int gen, int x, int y, int w, int h, const QImage &image);
    void failed(int eye, quint64 jobId, const QString &message);

private:
    bool cancelled() const;
    bool clipSupported(const QString &path, const QSize &full) const;
    QImage readOverview(const QString &path, const QSize &full) const;
    QImage readClipRect(const QString &path, const QSize &full, const QRect &src, int level) const;
    QImage readRasterPatch(int eye, const QRect &src, int level);

    QString m_path[2];
    QSize m_full[2];
    bool m_clipOk[2];
    QImage m_raster[2];
    QAtomicInt m_cancel;
    QAtomicInt m_winGen[2];
};

} // namespace qsv
