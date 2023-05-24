#include "variables.hpp"
#include <iostream>

namespace dtl {
// 枚举类型，用于指定输出文本的颜色
    enum class TextColor {
        BLACK,
        RED,
        GREEN,
        YELLOW,
        BLUE,
        MAGENTA,
        CYAN,
        WHITE
    };

// 重载运算符 <<，用于输出彩色文本
    std::ostream &operator<<(std::ostream &os, TextColor color) {
        switch (color) {
            case TextColor::BLACK:
                os << "\033[30m";
                break;
            case TextColor::RED:
                os << "\033[31m";
                break;
            case TextColor::GREEN:
                os << "\033[32m";
                break;
            case TextColor::YELLOW:
                os << "\033[33m";
                break;
            case TextColor::BLUE:
                os << "\033[34m";
                break;
            case TextColor::MAGENTA:
                os << "\033[35m";
                break;
            case TextColor::CYAN:
                os << "\033[36m";
                break;
            case TextColor::WHITE:
                os << "\033[37m";
                break;
            default:
                os << "\033[0m";
                break;
        }
        return os;
    }


    void resetColor(std::ostream &os) {
        std::cout << "\033[0m";
    }

}