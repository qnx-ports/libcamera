ifndef QCONFIG
QCONFIG=qconfig.mk
endif
include $(QCONFIG)

NAME=rpi_camera_ipa

#$(INSTALL_ROOT_$(OS)) is pointing to $QNX_TARGET
#by default, unless it was manually re-routed to
#a staging area by setting both INSTALL_ROOT_nto
#and USE_INSTALL_ROOT
INSTALL_ROOT ?= $(INSTALL_ROOT_$(OS))

#A prefix path to use **on the target**. This is
#different from INSTALL_ROOT, which refers to a
#installation destination **on the host machine**.
#This prefix path may be exposed to the source code,
#the linker, or package discovery config files (.pc,
#CMake config modules, etc.). Default is /usr
PREFIX ?= /usr

#choose Release or Debug
CMAKE_BUILD_TYPE ?= Release

#override 'all' target to bypass the default QNX build system
ALL_DEPENDENCIES = rpi_camera_ipa_all
.PHONY: rpi_camera_ipa_all rpi_camera_ipa_all_clean

FLAGS   += -D_QNX_SOURCE -D__QNXNTO__
LDFLAGS += -Wl,--build-id=md5 -lang-c++

include $(MKFILES_ROOT)/qtargets.mk

NTO_DIR_NAME=nto-aarch64-le
CMAKE_SYSTEM_PROCESSOR=aarch64

CMAKE_ARGS = -DCMAKE_TOOLCHAIN_FILE=$(PROJECT_ROOT)/qnx.nto.toolchain.cmake \
             -DCMAKE_SYSTEM_PROCESSOR=$(CMAKE_SYSTEM_PROCESSOR) \
             -DCMAKE_CXX_COMPILER_TARGET=gcc_nto$(CPUVARDIR) \
             -DCMAKE_INSTALL_PREFIX=$(INSTALL_ROOT)/$(CPUVARDIR)/$(PREFIX) \
             -DCMAKE_INSTALL_INCLUDEDIR=$(INSTALL_ROOT)/$(PREFIX)/include \
             -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) \
             -DBUILD_QNX_CXX_FLAGS="$(FLAGS)" \
             -DBUILD_QNX_LINKER_FLAGS="$(LDFLAGS)" \
             -DCMAKE_VERBOSE_MAKEFILE=1 \
             -DCMAKE_POSITION_INDEPENDENT_CODE=ON

ifndef NO_TARGET_OVERRIDE

MAKE_ARGS ?= -j $(firstword $(JLEVEL) 1)

rpi_camera_ipa_all:
	echo "make rpi_camera_ipa_all $(NTO_DIR_NAME)"
	mkdir -p build && \
	cd build && \
	cmake 	"${PRODUCT_ROOT}/build" \
		${CMAKE_ARGS} && \
	make all $(MAKE_ARGS)

rpi_camera_ipa_all_clean:
	echo "make rpi_camera_ipa_all_clean $(NTO_DIR_NAME)"
	cd $(PRODUCT_ROOT)/build && \
	rm -rf build
	rm -rf $(PROJECT_ROOT)/$(NTO_DIR_NAME)/build

clean: rpi_camera_ipa_all_clean

install check: rpi_camera_ipa_all
	cd build && make install $(MAKE_ARGS)

uninstall:
endif
