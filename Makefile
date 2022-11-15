.PHONY: all clean release clean-deps sign test

OBJECTS = c42-adbtool.o adb.o common.o crypto.o
SUBMODULES = cryptopp/Readme.txt zlib/README boost/README.md leveldb/README.md
BOOST_LIBS = boost/stage/lib/libboost_iostreams.a boost/stage/lib/libboost_program_options.a \
    boost/stage/lib/libboost_filesystem.a boost/stage/lib/libboost_system.a
STATIC_LIBS = leveldb/build/libleveldb.a cryptopp/libcryptopp.a $(BOOST_LIBS) zlib/libz.a

UNAME := $(shell uname)

MSYS_VERSION := $(if $(findstring Msys, $(shell uname -o)),$(word 1, $(subst ., ,$(shell uname -r))),0)

NUMPROCS = $(shell nproc || getconf _NPROCESSORS_ONLN || echo 2)

LINKER_OPTIONS = -Wall --std=c++14 -O3 -g3
COMPILER_OPTIONS = -Wall --std=c++14 -O3 -g3

#CODE_SIGNING_IDENTITY=Developer ID Application: Nicholas Sherlock (8J3T27D935)
#CODE_SIGNING_KEYCHAIN_PROFILE=n.sherlock

ifeq ($(MSYS_VERSION), 0)
	ifeq ($(UNAME), Darwin)
		# macOS
		LINKER_OPTIONS += -framework CoreFoundation -framework IOKit
	else 
		# Linux
		
		# Fix GCC bug 52590 https://gcc.gnu.org/bugzilla/show_bug.cgi?id=52590
		LINKER_OPTIONS += -Wl,--whole-archive -lpthread -lrt -Wl,--no-whole-archive
		COMPILER_OPTIONS += -pthread
	endif
else
	# Windows:
	LINKER_OPTIONS += -lcrypt32 -lole32 -luuid
endif

ifneq ($(UNAME), Darwin)
	# Can't make a static build on macOS, but the dynamic version works nicely anyway:
	LINKER_OPTIONS += -static -static-libgcc -static-libstdc++
	COMPILER_OPTIONS += -static -static-libgcc -static-libstdc++
endif
 
all : c42-adbtool

ZLIB_PATH = $(abspath zlib)

$(SUBMODULES) :
	git submodule update --init

cryptopp/libcryptopp.a :
	cd cryptopp && make -j $(NUMPROCS)

leveldb/build/libleveldb.a :
	mkdir -p leveldb/build
	cd leveldb/build && cmake .. -DCMAKE_BUILD_TYPE=Release -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF && cmake --build . -j $(NUMPROCS)

zlib/libz.a :
	cd zlib && ./configure && make -j $(NUMPROCS)

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
	$(CXX) -o $@ $(OBJECTS) comparator.o $(STATIC_LIBS) $(LINKER_OPTIONS) 

# Needs to be compiled separately so we can use fno-rtti to be compatible with leveldb:
comparator.o : comparator.cpp
	$(CXX) $(COMPILER_OPTIONS) -c -fno-rtti -o $@ -Ileveldb/include $<

%.o : %.cpp boost/boost/ $(STATIC_LIBS)
	 $(CXX) $(COMPILER_OPTIONS) -c -o $@ -Iboost -Ileveldb/include -Izlib  $<

ifdef CODE_SIGNING_IDENTITY
sign: c42-adbtool-macOS.zip 

c42-adbtool-macOS.zip : c42-adbtool
	codesign -s "$(CODE_SIGNING_IDENTITY)" --force --options=runtime --timestamp $<
	zip $@ $<
	xcrun notarytool \
		submit \
		--keychain-profile $(CODE_SIGNING_KEYCHAIN_PROFILE) \
		"$@"
endif

test: c42-adbtool
	rm -rf test/adb-temp
	cp -r test/adb test/adb-temp
	./c42-adbtool read --path test/adb-temp --key compliance_enforce --format hex | grep -q '^00$$'
	./c42-adbtool write --path test/adb-temp --key compliance_enforce --format hex --value 01 
	./c42-adbtool read --path test/adb-temp --key compliance_enforce --format hex | grep -q '^01$$'
	./c42-adbtool write --path test/adb-temp --key hello --value world
	./c42-adbtool read --path test/adb-temp --key hello | grep -q '^world$$'
	echo "there" | ./c42-adbtool write --path test/adb-temp --key hello
	./c42-adbtool read --path test/adb-temp --key hello | grep -q '^there$$'
	./c42-adbtool delete --path test/adb-temp --key hello
	echo "everybody" > test/adb-temp/hello
	./c42-adbtool write --path test/adb-temp --key hello --value-file test/adb-temp/hello
	./c42-adbtool read --path test/adb-temp --key hello | grep -q '^everybody$$'
	rm -rf test/adb-temp

clean :
	rm -f c42-adbtool c42-adbtool.exe *.o

clean-deps :
	cd cryptopp && make clean || true
	cd boost && ./b2 --clean || true
	rm -rf leveldb/build
	cd zlib && make clean || true
