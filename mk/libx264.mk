X264_VERSION=20170226-2245-stable

build/inst/%/lib/pkgconfig/libx264.pc: build/x264-snapshot-$(X264_VERSION)/build-%/Makefile
	cd build/x264-snapshot-$(X264_VERSION)/build-$* ; \
		emmake $(MAKE)
	cd build/x264-snapshot-$(X264_VERSION)/build-$* ; \
		emmake $(MAKE) install

build/x264-snapshot-$(X264_VERSION)/build-%/Makefile: build/x264-snapshot-$(X264_VERSION)/configure | build/inst/%/cflags.txt
	mkdir -p build/x264-snapshot-$(X264_VERSION)/build-$*
	cd build/x264-snapshot-$(X264_VERSION)/build-$* ; \
		emconfigure ../../x264-snapshot-$(X264_VERSION)/configure \
			--prefix="$(PWD)/build/inst/$*"\
			--host=i686-gnu \
			--enable-static --disable-shared \
			--disable-cli \
			--disable-asm \
			--extra-cflags="-s USE_PTHREADS=1  `cat $(PWD)/build/inst/$*/cflags.txt`"

extract: build/x264-snapshot-$(X264_VERSION)/configure

build/x264-snapshot-$(X264_VERSION)/configure: build/x264-snapshot-${X264_VERSION}.tar.bz2
	cd build ; tar xjvf x264-snapshot-${X264_VERSION}.tar.bz2
	touch $@

build/x264-snapshot-${X264_VERSION}.tar.bz2:
	mkdir -p build
	curl https://download.videolan.org/pub/videolan/x264/snapshots/x264-snapshot-${X264_VERSION}.tar.bz2 -L -o $@

libx264-release:
	cp build/x264-snapshot-${X264_VERSION}.tar.bz2 libav.js-$(LIBAVJS_VERSION)/sources/

.PRECIOUS: \
	build/inst/%/lib/pkgconfig/libx264.pc \
	build/x264-snapshot-$(X264_VERSION)/build-%/Makefile \
	build/x264-snapshot-$(X264_VERSION)/configure
