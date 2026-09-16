CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags gstreamer-1.0 glib-2.0)
LDLIBS += $(shell $(PKG_CONFIG) --libs gstreamer-1.0 glib-2.0)

PROGRAM := sp7-camera-backend-prototype
SOURCES := prototype.c media-backend.c
OBJECTS := $(SOURCES:.c=.o)

.PHONY: all clean sanitize

all: $(PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c media-backend.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

sanitize: CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean all

clean:
	$(RM) $(PROGRAM) $(OBJECTS) $(OBJECTS:.o=.d)

-include $(OBJECTS:.o=.d)
