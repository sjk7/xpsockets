TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.cpp \
        xpsockets.cpp

HEADERS += \
    tests/test_badly_behaved_client.h \
    xpcommon.h \
    xpsockets.h \
    xpsockets.hpp

win32:{
    LIBS += -lws2_32 -lwinmm
}

unix:{
    LIBS += -lpthread
}

CONFIG(release, debug|release) {
    #This is a release build
    defines += NDEBUG
} else {
    #This is a debug build
}
