TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.cpp \
        xpsockets.cpp

HEADERS += \
    xpcommon.h \
    xpsockets.h \
    xpsockets.hpp

win32:{
    LIBS += -lws2_32 -lwinmm
}

unix: release: DEFINES += NDEBUG
