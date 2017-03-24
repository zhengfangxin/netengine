.PHONY : install uninstall clean

CC ?= gcc
TARGET=libnetengine.a

BUILD_PATH ?= .

MYPUBINC=../../codelib
MYPUBLIB=../../codelib/lib

CFLAGS = -g -O2 -Wall -fPIC $(MYCFLAGS) -I../inc/ -I$(MYPUBINC) -D__linux__ --shared
# CFLAGS += -D_DEBUG -DDEBUG
SHARED := -fPIC --shared
LIB := -lpublib

ALLCFILE=accept accept_linux client client_linux connect connect_linux net net_linux net_buf net_malloc socket_poll

ALLOBJ=$(foreach v,$(ALLCFILE),obj/$(v).o)

#$(TARGET): $(ALLOBJ) 
#	$(CC) $(SHARED) -o ../lib/$(TARGET) $(ALLOBJ) $(LIB) -L$(MYPUBLIB)
$(TARGET): $(ALLOBJ) 
	ar crs ../lib/$(TARGET) $(ALLOBJ)

#create build directory
predir:
	-@mkdir -p lib >/dev/null 2>&1
	-@mkdir -p obj >/dev/null 2>&1
	-@mkdir -p dep >/dev/null 2>&1


$(ALLOBJ) : obj/%.o : %.c dep/%.d
	@echo "** Compiling" $< " **"
	-@mkdir -p $(dir $@) >/dev/null 2>&1
	$(CC) -c $(CFLAGS) $< -o $@

dep/%.d : %.c
	-@mkdir -p $(dir $@) >/dev/null 2>&1
	@set -e;  rm -f $@;\
	$(CC) -MM $(CFLAGS) $< > $@.$$$$; \
	sed 's,\(.*\)\.o[ :]*,obj/\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

sinclude $(addprefix dep/,$(addsuffix .d,$(ALLCFILE)))


install:
	cp -d ../lib/$(TARGET) /usr/local/lib/
	
uninstall:
	-$(RM) /usr/local/lib/$(TARGET)

clean:
	-$(RM) $(TARGET) obj/*.o dep/*	
ifneq (,$(wildcard obj))
	-rmdir obj
endif
ifneq (,$(wildcard dep))
	-rmdir dep
endif
