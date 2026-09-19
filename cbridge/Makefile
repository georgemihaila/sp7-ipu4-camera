CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2
CFLAGS += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags gstreamer-1.0 glib-2.0)
LDLIBS += $(shell $(PKG_CONFIG) --libs gstreamer-1.0 glib-2.0)

PROGRAM := sp7-camera-backend-prototype
SOURCES := prototype.c media-backend.c
OBJECTS := $(SOURCES:.c=.o)
CONTROLLER_PROGRAM := sp7-camera-bridge
CONTROLLER_SOURCES := surface-camera-bridge.c controller.c media-backend.c
CONTROLLER_OBJECTS := $(CONTROLLER_SOURCES:.c=.o)
CONTROLLER_TEST := controller-test
CONTROLLER_TEST_SOURCES := controller-test.c controller.c
CONTROLLER_TEST_OBJECTS := $(CONTROLLER_TEST_SOURCES:.c=.o)
IR_PROGRAM := sp7-camera-ir
IR_SOURCES := ir-camera-bridge.c ir-v4l2.c
IR_OBJECTS := $(IR_SOURCES:.c=.o)
IR_TEST_PROGRAM := ir-capture-test
IR_TEST_SOURCES := ir-capture-test.c ir-v4l2.c
IR_TEST_OBJECTS := $(IR_TEST_SOURCES:.c=.o)

.PHONY: all clean sanitize test-controller test-ir

all: $(PROGRAM) $(CONTROLLER_PROGRAM) $(IR_PROGRAM)

$(PROGRAM): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(CONTROLLER_PROGRAM): $(CONTROLLER_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CONTROLLER_OBJECTS) $(LDLIBS)

$(IR_PROGRAM): $(IR_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(IR_OBJECTS)

%.o: %.c media-backend.h controller.h ir-v4l2.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

sanitize: CFLAGS += -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
sanitize: LDFLAGS += -fsanitize=address,undefined
sanitize: clean all $(CONTROLLER_TEST)
	./$(CONTROLLER_TEST)

$(CONTROLLER_TEST): $(CONTROLLER_TEST_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(CONTROLLER_TEST_OBJECTS)

test-controller: $(CONTROLLER_TEST)
	./$(CONTROLLER_TEST)

$(IR_TEST_PROGRAM): $(IR_TEST_OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(IR_TEST_OBJECTS)

test-ir: $(IR_TEST_PROGRAM)
	./$(IR_TEST_PROGRAM)

clean:
	$(RM) $(PROGRAM) $(CONTROLLER_PROGRAM) $(CONTROLLER_TEST) $(OBJECTS) \
		$(CONTROLLER_OBJECTS) $(CONTROLLER_TEST_OBJECTS) \
		$(IR_PROGRAM) $(IR_TEST_PROGRAM) $(IR_OBJECTS) $(IR_TEST_OBJECTS) \
		$(OBJECTS:.o=.d) $(CONTROLLER_TEST_OBJECTS:.o=.d) \
		$(IR_OBJECTS:.o=.d) $(IR_TEST_OBJECTS:.o=.d)

-include $(OBJECTS:.o=.d) $(CONTROLLER_OBJECTS:.o=.d) \
	$(CONTROLLER_TEST_OBJECTS:.o=.d) $(IR_OBJECTS:.o=.d) \
	$(IR_TEST_OBJECTS:.o=.d)
