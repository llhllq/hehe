#pragma once

#include <QImage>

namespace qsv
{

// 生成可立即融合的示例立体对，便于无数据时验证显示链路。
void makeSampleStereoPair(QImage *left, QImage *right, int width = 1280, int height = 800);

} // namespace qsv
