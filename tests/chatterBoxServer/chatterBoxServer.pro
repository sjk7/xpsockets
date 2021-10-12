TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
    ../../xpcommon.cpp \
    ../../xpsockets.cpp \
    chatterBoxServer.cpp

HEADERS += \
    ../../xpcommon.h \
    ../../xpsockets.h \
    ../../xpsockets.hpp
