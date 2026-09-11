#pragma once

#include <QOpenGLShaderProgram>
#include <QString>

class QOpenGLContext;

namespace qsv
{

bool loadShaderFromQrc(QOpenGLShaderProgram *program,
                       const QString &vertPath,
                       const QString &fragPath,
                       QString *error);

} // namespace qsv
