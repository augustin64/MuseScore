
CPUS      := $(shell getconf _NPROCESSORS_ONLN 2>/dev/null || getconf NPROCESSORS_ONLN 2>/dev/null || echo 1)
PREFIX    = "/usr/local"

# EMBED_PRELOADS="ON"

#
# change path to include your Qt5 installation
#
BINPATH      = ${PATH}
PREFIX_PATH = ../../build.qt5/5.15.2/wasm_32

release:
	if test ! -d build.release; then mkdir build.release; fi; \
      cd build.release;                                        \
      export PATH=${BINPATH};                                   \
	  export CMAKE_PREFIX_PATH=${PREFIX_PATH};                   \
	  export NODE_OPTIONS=--max_old_space_size=4096;              \
      emcmake cmake -DCMAKE_BUILD_TYPE=RELEASE	                   \
	  -DEMBED_PRELOADS="${EMBED_PRELOADS}"                          \
  	  -DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}"               \
  	  -DCMAKE_INSTALL_PREFIX="${PREFIX}"                              \
  	  -DCMAKE_BUILD_NUMBER="${BUILD_NUMBER}" ..;                       \
      emmake make -j ${CPUS};                                           \
	  mv ./webmscore.* ../../web-public;                                 \

debug:
	if test ! -d build.debug; then mkdir build.debug; fi; \
      cd build.debug;                                      \
      export PATH=${BINPATH};                               \
	  export CMAKE_PREFIX_PATH=${PREFIX_PATH};               \
	  export NODE_OPTIONS=--max_old_space_size=4096;          \
      emcmake cmake -DCMAKE_BUILD_TYPE=DEBUG	               \
	  -DEMBED_PRELOADS="${EMBED_PRELOADS}"                      \
  	  -DCMAKE_TOOLCHAIN_FILE="${CMAKE_TOOLCHAIN_FILE}"           \
  	  -DCMAKE_INSTALL_PREFIX="${PREFIX}"                          \
  	  -DCMAKE_BUILD_NUMBER="${BUILD_NUMBER}" ..;                   \
      emmake make -j ${CPUS};                                       \
	  mv ./webmscore.* ../../web-public;                             \

#
# clean out of source build
#
clean:
	-rm -rf build.debug build.release
	-rm -rf ../web-public/.cache
	-rm ../web-public/webmscore.*
