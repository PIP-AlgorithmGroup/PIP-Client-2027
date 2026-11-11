#include "ui_fonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>

bool initializeUiFonts() {
    const auto font_id = QFontDatabase::addApplicationFont(":/resources/fonts/noto_sans_sc.ttf");
    if (font_id < 0) {
        return false;
    }
    const auto families = QFontDatabase::applicationFontFamilies(font_id);
    if (families.isEmpty()) {
        return false;
    }
    // 在 QML 默认字体解析前设置，Text 与 Controls 统一使用中文黑体，英文品牌另行指定。
    QFont font(families.first());
    font.setPixelSize(16);
    QGuiApplication::setFont(font);
    return true;
}
