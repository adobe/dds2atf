CXX:=g++
CC:=gcc
CCPARAMS:=-Os

INCLUDES=-I3rdparty/jpegxr -I3rdparty/lzma

JPEGXR_SRC=$(wildcard 3rdparty/jpegxr/*.cpp)
JPEGXR_OBJ=$(JPEGXR_SRC:.cpp=.o)

LZMA_SRC=$(wildcard 3rdparty/lzma/*.c)
LZMA_OBJ=$(LZMA_SRC:.c=.o)

.c.o:
	@echo CC $<
	@$(CC) $(CCPARAMS) $(INCLUDES) $(DEFINES) -c $< -o $@

.cpp.o:
	@echo CXX $<
	@$(CXX) $(CCPARAMS) $(INCLUDES) $(DEFINES) -c $< -o $@
	
all: $(JPEGXR_OBJ) $(LZMA_OBJ) dds2atf.o pvr2atfcore.o
	mkdir -p bin
	$(CXX) dds2atf.o pvr2atfcore.o 3rdparty/*/*.o -o bin/dds2tf

clean:
	rm -f bin/dds2atf *.o 3rdparty/*/*.o
