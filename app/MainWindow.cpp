#include "MainWindow.h"
#include "StereoCanvas.h"
#include "DisplayMode.h"

#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QSettings>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QDir>
#include <QShortcut>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QUrl>
#include <QWidget>

namespace qsv
{

static QSlider *makeSlider(int minV, int maxV, int value)
{
    QSlider *s = new QSlider(Qt::Horizontal);
    s->setRange(minV, maxV);
    s->setValue(value);
    return s;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_canvas(new StereoCanvas)
    , m_container(0)
    , m_modeBox(0)
    , m_brightL(0)
    , m_brightR(0)
    , m_contrastL(0)
    , m_contrastR(0)
    , m_gammaL(0)
    , m_gammaR(0)
    , m_parallaxSpin(0)
    , m_inverseBox(0)
    , m_swapInterlaceBox(0)
    , m_helpLabel(0)
    , m_updatingControls(false)
{
    setWindowTitle(QString::fromUtf8("QStereoView - 跨平台立体影像显示"));
    m_container = QWidget::createWindowContainer(m_canvas, this);
    m_container->setMinimumSize(320, 240);
    m_container->setFocusPolicy(Qt::StrongFocus);
    m_container->setMouseTracking(true);
    m_container->setAcceptDrops(true);
    m_container->setAttribute(Qt::WA_OpaquePaintEvent);
    m_container->setAttribute(Qt::WA_NoSystemBackground);
    m_container->installEventFilter(this);
    setAcceptDrops(true);
    installEventFilter(this);
    setCentralWidget(m_container);
    resize(1280, 800);

    createActions();
    createDocks();
    statusBar()->showMessage(QString::fromUtf8("就绪。可打开左右影像，或先加载示例立体对。"));

    connect(m_canvas, SIGNAL(statusChanged(QString)), this, SLOT(onStatus(QString)));
    connect(m_canvas, SIGNAL(viewChanged()), this, SLOT(syncControlsFromView()));

    m_canvas->loadSamplePair();
    readSettings();
}

void MainWindow::createActions()
{
    QMenu *fileMenu = menuBar()->addMenu(QString::fromUtf8("文件"));
    QToolBar *fileBar = addToolBar(QString::fromUtf8("文件"));
    fileBar->setMovable(false);

    QAction *actL = fileMenu->addAction(QString::fromUtf8("打开左片..."));
    actL->setShortcut(QKeySequence::Open);
    connect(actL, SIGNAL(triggered()), this, SLOT(openLeft()));
    fileBar->addAction(actL);

    QAction *actR = fileMenu->addAction(QString::fromUtf8("打开右片..."));
    actR->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_O));
    connect(actR, SIGNAL(triggered()), this, SLOT(openRight()));
    fileBar->addAction(actR);

    QAction *actPair = fileMenu->addAction(QString::fromUtf8("打开立体对..."));
    connect(actPair, SIGNAL(triggered()), this, SLOT(openPair()));
    fileBar->addAction(actPair);

    QAction *actSample = fileMenu->addAction(QString::fromUtf8("加载示例立体对"));
    connect(actSample, SIGNAL(triggered()), m_canvas, SLOT(loadSamplePair()));
    fileBar->addAction(actSample);

    fileMenu->addSeparator();
    QAction *actExport = fileMenu->addAction(QString::fromUtf8("导出当前视图..."));
    actExport->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_S));
    connect(actExport, SIGNAL(triggered()), this, SLOT(exportView()));

    fileMenu->addSeparator();
    QAction *actQuit = fileMenu->addAction(QString::fromUtf8("退出"));
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, SIGNAL(triggered()), this, SLOT(close()));

    QMenu *viewMenu = menuBar()->addMenu(QString::fromUtf8("视图"));
    QAction *actFit = viewMenu->addAction(QString::fromUtf8("适应窗口"));
    actFit->setShortcut(Qt::Key_F);
    connect(actFit, SIGNAL(triggered()), m_canvas, SLOT(fitView()));
    fileBar->addSeparator();
    fileBar->addAction(actFit);

    QAction *actReset = viewMenu->addAction(QString::fromUtf8("重置视图"));
    actReset->setShortcut(Qt::Key_Home);
    connect(actReset, SIGNAL(triggered()), m_canvas, SLOT(resetView()));

    viewMenu->addSeparator();
    QAction *zoomIn = viewMenu->addAction(QString::fromUtf8("放大"));
    zoomIn->setShortcut(QKeySequence::ZoomIn);
    connect(zoomIn, &QAction::triggered, this, [this]() { m_canvas->zoomBy(1.15); });
    QAction *zoomOut = viewMenu->addAction(QString::fromUtf8("缩小"));
    zoomOut->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOut, &QAction::triggered, this, [this]() { m_canvas->zoomBy(1.0 / 1.15); });

    auto addAppShortcut = [this](const QKeySequence &seq, void (MainWindow::*fn)()) {
        QShortcut *sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, fn);
    };
    addAppShortcut(QKeySequence(Qt::Key_Space), &MainWindow::cycleDisplayMode);

    auto addParallaxShortcut = [this](const QKeySequence &seq, double delta) {
        QShortcut *sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, delta]() { parallaxStep(delta); });
    };
    addParallaxShortcut(QKeySequence(Qt::Key_BracketLeft), -2.0);
    addParallaxShortcut(QKeySequence(Qt::Key_Minus), -2.0);
    addParallaxShortcut(QKeySequence(Qt::Key_BracketRight), 2.0);
    addParallaxShortcut(QKeySequence(Qt::Key_Equal), 2.0);
    addParallaxShortcut(QKeySequence(Qt::Key_Up), 4.0);
    addParallaxShortcut(QKeySequence(Qt::Key_Down), -4.0);

    auto addAlignShortcut = [this](const QKeySequence &seq, double dx, double dy) {
        QShortcut *sc = new QShortcut(seq, this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this, dx, dy]() { alignRightStep(dx, dy); });
    };
    addAlignShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Left), -10.0, 0.0);
    addAlignShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Right), 10.0, 0.0);
    addAlignShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Up), 0.0, -10.0);
    addAlignShortcut(QKeySequence(Qt::SHIFT + Qt::Key_Down), 0.0, 10.0);
    addAlignShortcut(QKeySequence(Qt::ALT + Qt::Key_Up), 0.0, -10.0);
    addAlignShortcut(QKeySequence(Qt::ALT + Qt::Key_Down), 0.0, 10.0);

    QShortcut *scInv = new QShortcut(QKeySequence(Qt::Key_I), this);
    scInv->setContext(Qt::ApplicationShortcut);
    connect(scInv, &QShortcut::activated, this, [this]() {
        m_inverseBox->toggle();
    });

    QMenu *helpMenu = menuBar()->addMenu(QString::fromUtf8("帮助"));
    QAction *actAbout = helpMenu->addAction(QString::fromUtf8("关于"));
    connect(actAbout, &QAction::triggered, this, [this]() {
        QMessageBox::information(this, QString::fromUtf8("关于 QStereoView"),
            QString::fromUtf8(
                "QStereoView 是一套基于 Qt + OpenGL 的立体影像显示系统。\n"
                "闪屏模式：有硬件四缓冲时左右眼同一帧输出真彩 RGB；\n"
                "无硬件时按刷新尽快翻页。红青/分屏为其他观察方式。\n\n"
                "滚轮：视差(高程)    Ctrl+滚轮：缩放\n"
                "Shift+滚轮：水平移动右片    Alt+滚轮：上下移动右片\n"
                "按住 Shift/Alt 时视图锁定右片，才能直接看见右片在动\n"
                "左键拖动：平移    右键或 Alt+左键：只移动右片对齐\n"
                "↑↓ 或 [ ]：视差    Shift+方向键：微调右片    空格：切换模式    I：反立体"));
    });
}

void MainWindow::createDocks()
{
    QDockWidget *dock = new QDockWidget(QString::fromUtf8("立体参数"), this);
    dock->setObjectName(QStringLiteral("StereoParams"));
    QWidget *panel = new QWidget(dock);
    QVBoxLayout *root = new QVBoxLayout(panel);

    m_modeBox = new QComboBox(panel);
    m_modeBox->addItem(QString::fromUtf8("闪屏 (真彩 RGB)"), int(DisplayMode::Shutter));
    m_modeBox->addItem(QString::fromUtf8("红青 (Dubois)"), int(DisplayMode::AnaglyphDubois));
    m_modeBox->addItem(QString::fromUtf8("红青 (简易)"), int(DisplayMode::AnaglyphSimple));
    m_modeBox->addItem(QString::fromUtf8("左右分屏"), int(DisplayMode::SideBySide));
    m_modeBox->addItem(QString::fromUtf8("右左分屏"), int(DisplayMode::SideBySideRL));
    m_modeBox->addItem(QString::fromUtf8("上下分屏"), int(DisplayMode::TopBottom));
    m_modeBox->addItem(QString::fromUtf8("行交错"), int(DisplayMode::RowInterlace));
    m_modeBox->addItem(QString::fromUtf8("列交错"), int(DisplayMode::ColumnInterlace));
    m_modeBox->addItem(QString::fromUtf8("棋盘"), int(DisplayMode::Checkerboard));
    m_modeBox->addItem(QString::fromUtf8("仅左片"), int(DisplayMode::LeftOnly));
    m_modeBox->addItem(QString::fromUtf8("仅右片"), int(DisplayMode::RightOnly));
    m_modeBox->addItem(QString::fromUtf8("差值对齐"), int(DisplayMode::Difference));
    connect(m_modeBox, SIGNAL(currentIndexChanged(int)), this, SLOT(onModeChanged(int)));

    QFormLayout *modeForm = new QFormLayout();
    modeForm->addRow(QString::fromUtf8("显示模式"), m_modeBox);
    m_inverseBox = new QCheckBox(QString::fromUtf8("反立体（交换左右眼）"), panel);
    connect(m_inverseBox, SIGNAL(toggled(bool)), m_canvas, SLOT(setInverseStereo(bool)));
    m_swapInterlaceBox = new QCheckBox(QString::fromUtf8("交错奇偶对调"), panel);
    connect(m_swapInterlaceBox, SIGNAL(toggled(bool)), m_canvas, SLOT(setSwapInterlace(bool)));
    modeForm->addRow(m_inverseBox);
    modeForm->addRow(m_swapInterlaceBox);
    root->addLayout(modeForm);

    m_parallaxSpin = new QDoubleSpinBox(panel);
    m_parallaxSpin->setRange(-800.0, 800.0);
    m_parallaxSpin->setDecimals(1);
    m_parallaxSpin->setSingleStep(1.0);
    m_parallaxSpin->setSuffix(QString::fromUtf8(" px"));
    connect(m_parallaxSpin, SIGNAL(valueChanged(double)), this, SLOT(onParallaxSpin(double)));
    QFormLayout *parForm = new QFormLayout();
    parForm->addRow(QString::fromUtf8("水平视差"), m_parallaxSpin);
    root->addLayout(parForm);

    auto addEyeGroup = [&](const QString &title, QSlider **b, QSlider **c, QSlider **g) {
        QGroupBox *box = new QGroupBox(title, panel);
        QFormLayout *form = new QFormLayout(box);
        *b = makeSlider(-50, 50, 0);
        *c = makeSlider(20, 300, 100);
        *g = makeSlider(40, 250, 100);
        form->addRow(QString::fromUtf8("亮度"), *b);
        form->addRow(QString::fromUtf8("对比度"), *c);
        form->addRow(QString::fromUtf8("伽马"), *g);
        connect(*b, SIGNAL(valueChanged(int)), this, SLOT(onAdjustChanged()));
        connect(*c, SIGNAL(valueChanged(int)), this, SLOT(onAdjustChanged()));
        connect(*g, SIGNAL(valueChanged(int)), this, SLOT(onAdjustChanged()));
        root->addWidget(box);
    };
    addEyeGroup(QString::fromUtf8("左片增强"), &m_brightL, &m_contrastL, &m_gammaL);
    addEyeGroup(QString::fromUtf8("右片增强"), &m_brightR, &m_contrastR, &m_gammaR);

    m_helpLabel = new QLabel(QString::fromUtf8(
        "操作：\n"
        "· 滚轮改视差，Ctrl+滚轮缩放\n"
        "· Shift+滚轮水平移右片，Alt+滚轮上下移右片\n"
        "· 按住 Shift/Alt 时画面切到右片，便于看见调整\n"
        "· 左键拖动平移，右键对齐右片\n"
        "· 空格切换模式，I 反立体\n"
        "· ↑↓ 或 [ ] 微调视差，Shift+方向键微调右片，F 适应窗口\n"
        "· 闪屏优先走硬件四缓冲真彩，不是红青\n"
        "· 可拖入 1~2 张影像文件"), panel);
    m_helpLabel->setWordWrap(true);
    root->addWidget(m_helpLabel);
    root->addStretch(1);

    dock->setWidget(panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::onModeChanged(int index)
{
    if (m_updatingControls)
        return;
    const int mode = m_modeBox->itemData(index).toInt();
    m_canvas->setDisplayMode(DisplayMode(mode));
}

void MainWindow::onAdjustChanged()
{
    if (m_updatingControls)
        return;
    m_canvas->leftAdjust().brightness = m_brightL->value() / 100.0f;
    m_canvas->leftAdjust().contrast = m_contrastL->value() / 100.0f;
    m_canvas->leftAdjust().gamma = m_gammaL->value() / 100.0f;
    m_canvas->rightAdjust().brightness = m_brightR->value() / 100.0f;
    m_canvas->rightAdjust().contrast = m_contrastR->value() / 100.0f;
    m_canvas->rightAdjust().gamma = m_gammaR->value() / 100.0f;
    m_canvas->notifyAdjustChanged();
}

void MainWindow::onParallaxSpin(double v)
{
    if (m_updatingControls)
        return;
    m_canvas->addParallax(v - m_canvas->camera().parallax());
}

void MainWindow::cycleDisplayMode()
{
    if (!m_modeBox)
        return;
    const int i = (m_modeBox->currentIndex() + 1) % m_modeBox->count();
    m_modeBox->setCurrentIndex(i);
}

void MainWindow::parallaxStep(double delta)
{
    m_canvas->addParallax(delta);
}

void MainWindow::alignRightStep(double dx, double dy)
{
    m_canvas->alignRightBy(dx, dy);
}

void MainWindow::onStatus(const QString &text)
{
    statusBar()->showMessage(text);
}

void MainWindow::syncControlsFromView()
{
    m_updatingControls = true;
    const int mode = int(m_canvas->displayMode());
    for (int i = 0; i < m_modeBox->count(); ++i)
    {
        if (m_modeBox->itemData(i).toInt() == mode)
        {
            m_modeBox->setCurrentIndex(i);
            break;
        }
    }
    m_inverseBox->setChecked(m_canvas->inverseStereo());
    m_parallaxSpin->setValue(m_canvas->camera().parallax());
    m_updatingControls = false;
}

QString MainWindow::chooseImage(const QString &title)
{
    return QFileDialog::getOpenFileName(
        this, title, QString(),
        QString::fromUtf8("影像 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp *.webp *.jp2);;所有文件 (*.*)"));
}

void MainWindow::openLeft()
{
    const QString path = chooseImage(QString::fromUtf8("打开左片"));
    if (path.isEmpty())
        return;
    if (!m_canvas->loadLeft(path))
        QMessageBox::warning(this, QString::fromUtf8("打开失败"), path);
}

void MainWindow::openRight()
{
    const QString path = chooseImage(QString::fromUtf8("打开右片"));
    if (path.isEmpty())
        return;
    if (!m_canvas->loadRight(path))
        QMessageBox::warning(this, QString::fromUtf8("打开失败"), path);
}

void MainWindow::openPair()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QString::fromUtf8("选择左片和右片（按顺序多选两张）"),
        QString(),
        QString::fromUtf8("影像 (*.tif *.tiff *.jpg *.jpeg *.png *.bmp *.webp *.jp2);;所有文件 (*.*)"));
    if (files.size() < 2)
        return;
    m_canvas->loadLeft(files[0]);
    m_canvas->loadRight(files[1]);
    m_canvas->fitView();
}

void MainWindow::exportView()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("导出当前视图"),
        QDir::homePath() + QStringLiteral("/stereo_view.png"),
        QString::fromUtf8("PNG (*.png);;JPEG (*.jpg)"));
    if (path.isEmpty())
        return;
    if (!m_canvas->exportView(path))
        QMessageBox::warning(this, QString::fromUtf8("导出失败"), path);
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt)
    {
        m_canvas->requestUpdate();
        QMainWindow::keyPressEvent(e);
        return;
    }
    switch (e->key())
    {
    case Qt::Key_Space:
    {
        int i = m_modeBox->currentIndex();
        i = (i + 1) % m_modeBox->count();
        m_modeBox->setCurrentIndex(i);
        break;
    }
    case Qt::Key_I:
        m_inverseBox->toggle();
        break;
    case Qt::Key_BracketLeft:
        m_canvas->addParallax(-2.0);
        syncControlsFromView();
        break;
    case Qt::Key_BracketRight:
        m_canvas->addParallax(2.0);
        syncControlsFromView();
        break;
    default:
        QMainWindow::keyPressEvent(e);
        break;
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Alt)
        m_canvas->requestUpdate();
    QMainWindow::keyReleaseEvent(e);
}

void MainWindow::readSettings()
{
    QSettings s(QStringLiteral("QStereoView"), QStringLiteral("QStereoView"));
    restoreGeometry(s.value(QStringLiteral("geometry")).toByteArray());
    restoreState(s.value(QStringLiteral("state")).toByteArray());
}

void MainWindow::writeSettings()
{
    QSettings s(QStringLiteral("QStereoView"), QStringLiteral("QStereoView"));
    s.setValue(QStringLiteral("geometry"), saveGeometry());
    s.setValue(QStringLiteral("state"), saveState());
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    const bool dropTarget = (obj == m_container || obj == this);
    if (dropTarget && e->type() == QEvent::DragEnter)
    {
        QDragEnterEvent *de = static_cast<QDragEnterEvent *>(e);
        if (de->mimeData()->hasUrls())
        {
            de->acceptProposedAction();
            return true;
        }
    }
    if (dropTarget && e->type() == QEvent::DragMove)
    {
        QDragMoveEvent *de = static_cast<QDragMoveEvent *>(e);
        if (de->mimeData()->hasUrls())
        {
            de->acceptProposedAction();
            return true;
        }
    }
    if (dropTarget && e->type() == QEvent::Drop)
    {
        QDropEvent *de = static_cast<QDropEvent *>(e);
        if (m_canvas->handleDropUrls(de->mimeData()->urls()))
        {
            de->acceptProposedAction();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    writeSettings();
    QMainWindow::closeEvent(e);
}

} // namespace qsv
