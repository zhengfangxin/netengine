CC ?= gcc
TARGET=test_client

BUILD_PATH ?= .

MYPUBINC=/home/life/src/mycode/publib
MYPUBLIB=/home/life/src/mycode/publib/lib

CFLAGS = -g -O2 -Wall $(MYCFLAGS) -I../inc/ -I../test_server/inc -I$(MYPUBINC) -D_linux
# CFLAGS += -D_DEBUG -DDEBUG

LIB := -lpublib -lpthread
MYLIB := -L../lib -lnetengine

ALLCFILE=test_client

ALLOBJ=$(foreach v,$(ALLCFILE),obj/$(v).o)

.PHONY : clean

$(TARGET): $(ALLOBJ) 
	$(CC) $(SHARED) -o ./$(TARGET) $(ALLOBJ) $(LIB) -L$(MYPUBLIB) $(MYLIB)

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


clean:
	-$(RM) $(TARGET) obj/*.o dep/*
ifneq (,$(wildcard obj))
	-rmdir obj
endif
ifneq (,$(wildcard dep))
	-rmdir dep
endif

