#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>

namespace qsv
{
class StereoCanvas;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = 0);

protected:
    void closeEvent(QCloseEvent *e) Q_DECL_OVERRIDE;
    void keyPressEvent(QKeyEvent *e) Q_DECL_OVERRIDE;
    void keyReleaseEvent(QKeyEvent *e) Q_DECL_OVERRIDE;
    bool eventFilter(QObject *obj, QEvent *e) Q_DECL_OVERRIDE;

private slots:
    void openLeft();
    void openRight();
    void openPair();
    void exportView();
    void onModeChanged(int index);
    void onAdjustChanged();
    void onParallaxSpin(double v);
    void onStatus(const QString &text);
    void syncControlsFromView();
    void cycleDisplayMode();
    void parallaxStep(double delta);
    void alignRightStep(double dx, double dy);

private:
    void createActions();
    void createDocks();
    void readSettings();
    void writeSettings();
    QString chooseImage(const QString &title);

    StereoCanvas *m_canvas;
    QWidget *m_container;
    QComboBox *m_modeBox;
    QSlider *m_brightL;
    QSlider *m_brightR;
    QSlider *m_contrastL;
    QSlider *m_contrastR;
    QSlider *m_gammaL;
    QSlider *m_gammaR;
    QDoubleSpinBox *m_parallaxSpin;
    QCheckBox *m_inverseBox;
    QCheckBox *m_swapInterlaceBox;
    QLabel *m_helpLabel;
    bool m_updatingControls;
};

} // namespace qsv
