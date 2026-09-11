#pragma once

namespace qsv
{

enum class DisplayMode
{
    AnaglyphSimple = 0,   // 红青简易合成
    AnaglyphDubois,       // Dubois 红青，串扰更少
    SideBySide,           // 左右分屏
    SideBySideRL,         // 右|左分屏
    TopBottom,            // 上下分屏
    RowInterlace,         // 行交错
    ColumnInterlace,      // 列交错
    Checkerboard,         // 棋盘（部分 3D 电视）
    LeftOnly,             // 只看左片
    RightOnly,            // 只看右片
    Difference,           // 差值，用于检查相对定向/对齐
    Shutter               // 闪屏：左右眼交替输出真彩 RGB，不走红青
};

inline const char *displayModeKey(DisplayMode m)
{
    switch (m)
    {
    case DisplayMode::AnaglyphSimple: return "anaglyph";
    case DisplayMode::AnaglyphDubois: return "anaglyph_dubois";
    case DisplayMode::SideBySide: return "sbs";
    case DisplayMode::SideBySideRL: return "sbs_rl";
    case DisplayMode::TopBottom: return "tb";
    case DisplayMode::RowInterlace: return "row";
    case DisplayMode::ColumnInterlace: return "col";
    case DisplayMode::Checkerboard: return "checker";
    case DisplayMode::LeftOnly: return "left";
    case DisplayMode::RightOnly: return "right";
    case DisplayMode::Difference: return "diff";
    case DisplayMode::Shutter: return "shutter";
    }
    return "anaglyph";
}

} // namespace qsv
