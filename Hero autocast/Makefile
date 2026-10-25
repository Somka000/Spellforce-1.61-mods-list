CC = g++
DLL_CFLAGS = -O1 -g -std=c++11 ${WARNS} -Iinclude -DADD_EXPORTS -fpermissive -m32 ${EXTRA}
DLL_LDFLAGS = -m32 -shared -static-libgcc -static-libstdc++ -s -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -Wl,--subsystem,windows,--out-implib,lib/hero_autocast.a
MOD_OBJ = obj/hero_autocast.o

.PHONY: all clean 

# Default target
all: bin/hero_autocast.sfm

# Target for cleaning up
clean:
	if exist bin\* del /q bin\*
	if exist lib\* del /q lib\*
	if exist obj\* del /q obj\*

# Target for creating directories
bin lib obj:
	@if not exist "$@" mkdir "$@"

#mod build
obj/hero_autocast.o: hero_autocast.cpp | obj
	${CC} ${DLL_CFLAGS} -c "$<" -o "$@"

bin/hero_autocast.sfm: ${MOD_OBJ} | bin lib
	${CC} -o "$@" ${MOD_OBJ} ${DLL_LDFLAGS}
