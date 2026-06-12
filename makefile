CC      = emcc
TARGET  = ruby_converter.js

ZLIB_SRC = \
    zlib/adler32.c \
    zlib/crc32.c \
    zlib/deflate.c \
    zlib/inffast.c \
    zlib/inflate.c \
    zlib/inftrees.c \
    zlib/trees.c \
    zlib/uncompr.c \
    zlib/zutil.c

MINIZIP_SRC = minizip/unzip.c minizip/ioapi.c

SRCS = docx.c $(MINIZIP_SRC) $(ZLIB_SRC)

CFLAGS = -O2 -Izlib -Iminizip

EMFLAGS = \
    -sEXPORTED_FUNCTIONS='["_docx_to_html_wasm","_malloc","_free"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","FS"]' \
    -sALLOW_MEMORY_GROWTH=1 \
    -sENVIRONMENT=web \
    -sFORCE_FILESYSTEM=1

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(EMFLAGS) $(SRCS) -o $(TARGET)

clean:
	del $(TARGET) ruby_converter.wasm