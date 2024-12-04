
VERSION  =1.00
CC   =gcc
CPP =g++
LD = g++
DEBUG   =-DUSE_DEBUG
CFLAGS  += -Wall
SRC_C   = $(wildcard ../resources/*.c) $(wildcard ./*.c)
SRC_CPP= $(wildcard ./*.cpp)
INCLUDES   =-I. -I../resources -I./include
TEST_SET?=$(wildcard ./test_*.txt)
# LIB_NAMES  =-lfun_a -lfun_so
# LIB_PATH  =-L./lib
OBJ   =$(patsubst %.c, %.o, $(SRC_C)) $(patsubst %.cpp, %.o, $(SRC_CPP))
TARGET  =app_$(VERSION)
#links
$(TARGET):$(OBJ)
	@mkdir -p build
	@echo + LD $(TARGET)
	@$(LD) $(OBJ) $(LIB_PATH) $(LIB_NAMES) -o build/$(TARGET)
	@rm -rf $(OBJ)

ifeq ($(MAKECMDGOALS),test)
  CFLAGS += -DAUTO_TEST
endif

#compile
%.o: %.c
	@echo "+ CC $<"
	@$(CC) $(INCLUDES) $(DEBUG) -c $(CFLAGS) $< -o $@

%.o: %.cpp
	@echo "+ CC $<"
	@$(CC) $(INCLUDES) $(DEBUG) -c $(CFLAGS) $< -o $@


run:$(TARGET)
	@./build/$(TARGET)

test:$(TARGET)
	cat ${TEST_SET} | ./build/$(TARGET)


.PHONY:clean
clean:
	@echo "Remove linked and compiled files......"
	rm -rf $(OBJ) $(TARGET) build