# Makefile for UWB Driver Example, inspired by official SDK examples.

# --- Environment Checks ---
# Stop with an error if envsetup.sh has not been sourced.
ifeq (,$(TOOLCHAIN_PREFIX))
$(error ERROR: TOOLCHAIN_PREFIX is not set. Please source the 'envsetup.sh' script from your SDK first.)
endif
ifeq (,$(CFLAGS))
$(error ERROR: CFLAGS is not set. Please source the 'envsetup.sh' script from your SDK first.)
endif
ifeq (,$(LDFLAGS))
$(error ERROR: LDFLAGS is not set. Please source the 'envsetup.sh' script from your SDK first.)
endif

# --- Toolchain and Compiler ---
# Use the compiler defined by the environment script.
CC = $(TOOLCHAIN_PREFIX)gcc

# --- Project Structure & Paths ---
PROJ_ROOT 	= ..
# VPATH tells 'make' where to look for source files.
VPATH = .:$(PROJ_ROOT)/src:$(PROJ_ROOT)/hal/milkv_duo

# --- Compilation Flags ---
# Use CFLAGS from environment and add project-specific include paths.
CFLAGS += -Wall -Wextra -g
CFLAGS += -I.
CFLAGS += -I$(PROJ_ROOT)/include
CFLAGS += -I$(PROJ_ROOT)/hal/include

# --- Linker Flags ---
# Use LDFLAGS from environment and add project-specific library paths and libraries.
LDFLAGS += -L/home/cl/MilkV_Duo_project/duo-project/libs/system/musl_riscv64
LDFLAGS += -lwiringx -lpthread

# --- Files and Output ---
TARGET = uwb_mk8000

# Source file basenames. VPATH will be used to find them.
SRCS_NAMES = main.c uwb_core.c uwb_at.c hal_milkv_duo.c

# Object file names will be created in the current directory.
OBJS = $(SRCS_NAMES:.c=.o)

# --- Rules ---
.PHONY: all clean

# Default target
all: $(TARGET)

# Rule to link the final executable
$(TARGET): $(OBJS)
	@echo "  LD      $@"
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "  CLEANUP Object files"
	@rm -f $(OBJS)
	@echo "Build finished. Executable is '$(TARGET)' in the current directory."

# Generic rule to compile any .c file found via VPATH into an object file
# in the current directory.
%.o: %.c
	@echo "  CC      $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	@echo "  CLEAN"
	@rm -f $(TARGET) $(OBJS)
