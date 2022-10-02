.PHONY:  all clean dist-clean install

all: .build/Makefile log version.h
	$(MAKE) -C .build all

clean: .build/Makefile 
	$(MAKE) -C .build clean

distclean: clean 
	rm -rf .build

install: .build/Makefile
	$(MAKE) -C .build install	

.build/Makefile: CMakeLists.txt
	git submodule update --init
	mkdir -p .build
	cmake -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++ -S . -B .build
	 
log:
	mkdir -p log
	
version.h: .git/index Makefile 	 
	GIT=`which git`;VER=`$$GIT describe --tags`;echo "#define GIT_PROJECT_VERSION \"$$VER\"" > ./version.h