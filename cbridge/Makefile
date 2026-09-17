CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags gstreamer-1.0 gstreamer-app-1.0 glib-2.0)
LDLIBS += $(shell $(PKG_CONFIG) --libs gstreamer-1.0 gstreamer-app-1.0 glib-2.0)

PROGRAM := sp7-camera-backend-prototype
SOURCES := prototype.c media-backend.c
OBJECTS := $(SOURCES:.c=.o)
CONTROLLER_PROGRAM := sp7-camera-bridge
CONTROLLER_SOURCES := surface-camera-bridge.c controller.c media-backend.c
CONTROLLER_OBJECTS := $(CONTROLLER_SOURCES:.c=.o)
CONTROLLER_TEST := controller-test
CONTROLLER_TEST_SOURCES := controller-test.c controller.c
CONTROLLER_TEST_OBJECTS := $(CONTROLLER_TEST_SOURCES:.c=.o)

.PHONY: all clean sanitize test-controller

all: $(PROGRAM) $(CONTROLLER_PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(CONTROLLER_PROGRAM): $(CONTROLLER_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CONTROLLER_OBJECTS) $(LDLIBS)

%.o: %.c media-backend.h controller.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

sanitize: CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean all $(CONTROLLER_TEST)
	./$(CONTROLLER_TEST)

$(CONTROLLER_TEST): $(CONTROLLER_TEST_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CONTROLLER_TEST_OBJECTS)

test-controller: $(CONTROLLER_TEST)
	./$(CONTROLLER_TEST)

clean:
	$(RM) $(PROGRAM) $(CONTROLLER_PROGRAM) $(CONTROLLER_TEST) $(OBJECTS) \
		$(CONTROLLER_OBJECTS) $(CONTROLLER_TEST_OBJECTS) \
		$(OBJECTS:.o=.d) $(CONTROLLER_TEST_OBJECTS:.o=.d)

-include $(OBJECTS:.o=.d) $(CONTROLLER_OBJECTS:.o=.d) \
	$(CONTROLLER_TEST_OBJECTS:.o=.d)
