RACK_DIR ?= ../..
#RACK_DIR=~/Rack-SDK-1.1.1

# I wish there was a makefile option to remove '-g' flag. from this file
FLAGS +=
#FLAGS += -w
CFLAGS +=
CXXFLAGS +=

LDFLAGS +=

SOURCES += src/Autinn.cpp
SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res

include $(RACK_DIR)/plugin.mk