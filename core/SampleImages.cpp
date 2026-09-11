#include "SampleImages.h"

#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QLinearGradient>
#include <QFont>

namespace qsv
{

static void drawScene(QPainter *p, int w, int h, int disparityScale)
{
    QLinearGradient sky(0, 0, 0, h);
    sky.setColorAt(0.0, QColor(36, 54, 92));
    sky.setColorAt(0.55, QColor(78, 110, 148));
    sky.setColorAt(1.0, QColor(48, 78, 58));
    p->fillRect(0, 0, w, h, sky);

    p->setPen(QPen(QColor(255, 255, 255, 40), 1));
    const int grid = 40;
    for (int x = 0; x <= w; x += grid)
        p->drawLine(x, 0, x, h);
    for (int y = 0; y <= h; y += grid)
        p->drawLine(0, y, w, y);

    struct Box
    {
        QRect rect;
        QColor color;
        int depth; // 越大越近，视差越大
        QString label;
    };

    const Box boxes[] = {
        {QRect(90, 520, 1100, 90), QColor(46, 120, 72), 4, QString::fromUtf8("远地面")},
        {QRect(160, 360, 280, 160), QColor(196, 86, 64), 18, QString::fromUtf8("近物 A")},
        {QRect(520, 300, 220, 220), QColor(70, 140, 210), 12, QString::fromUtf8("中物 B")},
        {QRect(820, 250, 180, 260), QColor(210, 180, 70), 22, QString::fromUtf8("近物 C")},
        {QRect(400, 140, 140, 90), QColor(190, 190, 200), 8, QString::fromUtf8("远物 D")},
    };

    QFont font;
    font.setPixelSize(18);
    p->setFont(font);

    for (const Box &b : boxes)
    {
        const int shift = b.depth * disparityScale;
        const QRect r = b.rect.translated(shift, 0);
        p->setBrush(b.color);
        p->setPen(QPen(b.color.darker(140), 2));
        p->drawRoundedRect(r, 8, 8);
        p->setPen(Qt::white);
        p->drawText(r, Qt::AlignCenter, b.label);
    }

    p->setPen(QPen(Qt::white, 2));
    p->drawText(QRect(20, 16, w - 40, 40), Qt::AlignLeft | Qt::AlignVCenter,
                QString::fromUtf8("QStereoView 示例立体对  |  红青眼镜或分屏查看"));
}

void makeSampleStereoPair(QImage *left, QImage *right, int width, int height)
{
    if (!left || !right)
        return;

    *left = QImage(width, height, QImage::Format_RGB32);
    *right = QImage(width, height, QImage::Format_RGB32);
    left->fill(Qt::black);
    right->fill(Qt::black);

    {
        QPainter p(left);
        p.setRenderHint(QPainter::Antialiasing, true);
        drawScene(&p, width, height, 0);
    }
    {
        QPainter p(right);
        p.setRenderHint(QPainter::Antialiasing, true);
        drawScene(&p, width, height, 1);
    }
}

} // namespace qsv
