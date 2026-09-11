#include "GlShader.h"

#include <QFile>
#include <QTextStream>
#include <QOpenGLShader>

namespace qsv
{

static QByteArray readQrc(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QString::fromUtf8("无法打开着色器: %1").arg(path);
        return QByteArray();
    }
    return f.readAll();
}

bool loadShaderFromQrc(QOpenGLShaderProgram *program,
                       const QString &vertPath,
                       const QString &fragPath,
                       QString *error)
{
    if (!program)
        return false;

    program->removeAllShaders();

    const QByteArray vert = readQrc(vertPath, error);
    const QByteArray frag = readQrc(fragPath, error);
    if (vert.isEmpty() || frag.isEmpty())
        return false;

    if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vert))
    {
        if (error)
            *error = program->log();
        return false;
    }
    if (!program->addShaderFromSourceCode(QOpenGLShader::Fragment, frag))
    {
        if (error)
            *error = program->log();
        return false;
    }
    if (!program->link())
    {
        if (error)
            *error = program->log();
        return false;
    }
    return true;
}

} // namespace qsv
