RACK_DIR ?= ../..
#RACK_DIR=~/Rack-SDK-2.0.0

FLAGS +=
#FLAGS += -w
CFLAGS +=
CXXFLAGS +=

LDFLAGS +=

SOURCES += src/Autinn.cpp
SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res

include $(RACK_DIR)/plugin.mk