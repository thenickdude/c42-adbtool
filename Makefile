.PHONY: all clean release clean-deps

OBJECTS = c42-adbtool.o adb.o common.o crypto.o
SUBMODULES = cryptopp/Readme.txt zlib/README boost/README.md leveldb/README.md
BOOST_LIBS = boost/stage/lib/libboost_iostreams.a boost/stage/lib/libboost_program_options.a \
    boost/stage/lib/libboost_filesystem.a boost/stage/lib/libboost_system.a
STATIC_LIBS = leveldb/build/libleveldb.a cryptopp/libcryptopp.a $(BOOST_LIBS) zlib/libz.a

UNAME := $(shell uname)

MSYS_VERSION := $(if $(findstring Msys, $(shell uname -o)),$(word 1, $(subst ., ,$(shell uname -r))),0)

ifeq ($(MSYS_VERSION), 0)
	ifeq ($(UNAME), Darwin)
	# macOS
	LINK_OS_LIBS = -framework CoreFoundation -framework IOKit
	else 
	# Linux
	LINK_OS_LIBS =
	endif
else
# Windows:
LINK_OS_LIBS = -lcrypt32 -lole32 -luuid
endif

ifeq ($(UNAME), Darwin)
# Can't make a static build on macOS, but the dynamic version works nicely anyway:
STATIC_OPTIONS =
else
STATIC_OPTIONS = -static -static-libgcc -static-libstdc++
endif
 
all : c42-adbtool

ZLIB_PATH = $(abspath zlib)

$(SUBMODULES) :
	git submodule update --init

cryptopp/libcryptopp.a :
	cd cryptopp && make

leveldb/build/libleveldb.a :
	mkdir -p leveldb/build
	cd leveldb/build && cmake .. -DCMAKE_BUILD_TYPE=Release -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF && cmake --build .

zlib/libz.a :
	cd zlib && ./configure && make

boost/boost/ : zlib/libz.a
	cd boost && git submodule update --init && ./bootstrap.sh
	echo "using zlib : 1.2.11 : <include>$(ZLIB_PATH) <search>$(ZLIB_PATH) ;" >> boost/project-config.jam
	cd boost && ./b2 headers
	touch -c boost/boost # Ensure it becomes newer than libz so we don't keep rebuilding it

$(BOOST_LIBS) : zlib/libz.a
	cd boost && ./b2 stage variant=release threading=multi link=static address-model=64 --layout=system --build-type=minimal \
		--with-program_options --with-filesystem --with-iostreams --with-system -s NO_BZIP2=1
	touch -c $(BOOST_LIBS) # Ensure it becomes newer than libz so we don't keep rebuilding it

c42-adbtool : $(SUBMODULES) $(OBJECTS) comparator.o $(STATIC_LIBS)
	$(CXX) $(STATIC_OPTIONS) -Wall --std=c++14 -O3 -g3 -o $@ $(OBJECTS) comparator.o $(STATIC_LIBS) $(LINK_OS_LIBS)

# Needs to be compiled separately so we can use fno-rtti to be compatible with leveldb:
comparator.o : comparator.cpp
	$(CXX) $(STATIC_OPTIONS) -c -fno-rtti -Wall --std=c++14 -O3 -g3 -o $@ -Ileveldb/include $<

%.o : %.cpp boost/boost/ $(STATIC_LIBS)
	 $(CXX) $(STATIC_OPTIONS) -c -Wall --std=c++14 -O3 -g3 -o $@ -Iboost -Ileveldb/include -Izlib  $<

clean :
	rm -f c42-adbtool c42-adbtool.exe *.o

clean-deps :
	cd cryptopp && make clean || true
	cd boost && ./b2 --clean || true
	rm -rf leveldb/build
	cd zlib && make clean || true
