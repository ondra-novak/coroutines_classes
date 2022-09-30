.PHONY:  all clean install

all: .build/Makefile log version.h
	$(MAKE) -C .build all

clean: .build/Makefile 
	$(MAKE) -C .build clean
	rm -rf .build

install: .build/Makefile
	$(MAKE) -C .build install	

.build/Makefile: CMakeLists.txt .git/HEAD
	git submodule update --init
	mkdir -p .build
	cmake -S . -B .build
	 
log:
	mkdir -p log
	
version.h: .git/HEAD Makefile 	 
	GIT=`which git`;VER=`$$GIT describe --tags`;echo "#define GIT_PROJECT_VERSION \"$$VER\"" > ./version.h
