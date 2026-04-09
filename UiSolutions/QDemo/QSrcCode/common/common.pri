##
## 说明：
## 本子模块(common.pri)用作：用户对第三方模块的管理(库或者源代码)
##

INCLUDEPATH += \
    $$PWD/ \
    $$PWD/player/ \

SOURCES += \
    $$PWD/player/player.cpp \

HEADERS += \
    $$PWD/system.h \
    $$PWD/player/player.h \
