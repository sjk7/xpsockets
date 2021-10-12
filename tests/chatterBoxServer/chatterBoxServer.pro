TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    ../../xpcommon.cpp \
    ../../xpsockets.cpp \
    chatterBoxServer.cpp

HEADERS += \
    ../../http.hpp \
    ../../strings.hpp \
    ../../xpcommon.h \
    ../../xpsockets.h \
    ../../xpsockets.hpp
