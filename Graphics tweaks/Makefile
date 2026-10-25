CC = g++
DLL_CFLAGS = -O1 -g -std=c++11 ${WARNS} -Iinclude -DADD_EXPORTS -fpermissive -m32
DLL_LDFLAGS = -m32 -shared -static-libgcc -static-libstdc++ -s -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive -Wl,--subsystem,windows,--out-implib,lib/graphics_tweaks.a
MOD_OBJ = obj/graphics_tweaks.o

.PHONY: all clean 

# Default target
all: bin/graphics_tweaks.sfm

# Target for cleaning up
clean:
	if exist bin\* del /q bin\*
	if exist lib\* del /q lib\*
	if exist obj\* del /q obj\*

# Target for creating directories
bin lib obj:
	@if not exist "$@" mkdir "$@"

#mod build
obj/graphics_tweaks.o: graphics_tweaks.cpp | obj
	${CC} ${DLL_CFLAGS} -c "$<" -o "$@"

bin/graphics_tweaks.sfm: ${MOD_OBJ} | bin lib
	${CC} -o "$@" ${MOD_OBJ} ${DLL_LDFLAGS}
