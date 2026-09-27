# The app itself. build.sh must have run first: it builds QDOS's core and the
# Quadrate libraries this links, which the RPM's %build step does.

TARGET = harbour-r8-one

CONFIG += sailfishapp c++11

SOURCES += \
    src/main.cpp \
    src/machine.cpp \
    src/qdosview.cpp

HEADERS += \
    src/machine.h \
    src/qdosview.h

OTHER_FILES += \
    qml/harbour-r8-one.qml \
    qml/pages/CalculatorPage.qml \
    qml/cover/CoverPage.qml \
    rpm/harbour-r8-one.spec \
    harbour-r8-one.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# Where build.sh leaves the native libraries
isEmpty(R8_NATIVE): R8_NATIVE = $$PWD/build/native

INCLUDEPATH += \
    $$PWD/external/qdos/include \
    $$PWD/external/qdos/src

# A group, as the core and Quadrate's libraries reach into each other
R8_LIBS = \
    $$R8_NATIVE/lib/libqdos_core.a \
    $$R8_NATIVE/lib/libinterp.a \
    $$R8_NATIVE/lib/libqc.a \
    $$R8_NATIVE/lib/libmath.a \
    $$R8_NATIVE/lib/librt.a \
    $$R8_NATIVE/lib/libu8t.a
LIBS += -Wl,--start-group $$R8_LIBS -Wl,--end-group -ldl -lm -lpthread
PRE_TARGETDEPS += $$R8_LIBS

# GCC 8, which the SDK has, keeps std::filesystem in a library of its own
system($$QMAKE_CXX -dumpversion | grep -q "^8\\."): LIBS += -lstdc++fs

# The programs QDOS ships with: system is read in place, user seeds the store
programs.files = $$PWD/external/qdos/programs/system $$PWD/external/qdos/programs/user
programs.path = /usr/share/$${TARGET}/programs
INSTALLS += programs
