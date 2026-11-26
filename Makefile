# SS2CoopLobby - coop lobby for Serious Sam 2 (Steam, v2.224).
#
# Builds the three files to copy into the game folder, in the game's own layout:
#   out/Bin/SS2CoopLobby.dll      the mod (Win32 DLL, no CRT, no Windows SDK)
#   out/Bin/SS2CoopLobby.ini      its settings
#   out/Content/SeriousSam2.mod   the game's module list, with SS2CoopLobby added
#
# Requirements: GNU make, clang, lld-link, llvm-dlltool and llvm-objdump (known to work with LLVM 21
# and 23).
# Nothing from the game is needed: the mod looks up the game's functions when the game loads it.

CLANG   ?= clang
LLD     ?= lld-link
DLLTOOL ?= llvm-dlltool
OBJDUMP ?= llvm-objdump

OUT := out
OBJ := $(OUT)/obj

DLL := $(OUT)/Bin/SS2CoopLobby.dll
INI := $(OUT)/Bin/SS2CoopLobby.ini
MOD := $(OUT)/Content/SeriousSam2.mod

SRCS := $(wildcard src/*.cpp)
HDRS := $(wildcard src/*.h)
OBJS := $(SRCS:src/%.cpp=$(OBJ)/%.obj)
LIBS := $(OBJ)/kernel32.lib $(OBJ)/user32.lib $(OBJ)/msvcr71.lib

# -mno-stack-arg-probe: there is no CRT, hence no __chkstk (see the check after linking).
CXXFLAGS := --target=i686-pc-windows-msvc -O2 -msse2 -ffreestanding -fms-extensions \
            -fno-exceptions -fno-rtti -fno-stack-protector -fno-builtin -mno-stack-arg-probe \
            -Wall -Wno-unused-function -Wno-missing-braces
LDFLAGS  := /dll /nodefaultlib /noimplib /entry:_DllMainCRTStartup /subsystem:windows,5.01 \
            /safeseh:no

.PHONY: all clean
.DELETE_ON_ERROR:

all: $(DLL) $(INI) $(MOD)

$(OBJ)/%.obj: src/%.cpp $(HDRS) | $(OBJ)
	$(CLANG) $(CXXFLAGS) -c $< -o $@

# Import libraries made from the lists in def/: names only, no code.
$(OBJ)/%.lib: def/%.def | $(OBJ)
	$(DLLTOOL) -k -m i386 -d $< -l $@

# Without stack probes, a stack frame of 4 KiB or more could skip the stack's guard page and crash
# on Windows: such frames are refused (and the build fails if the disassembly cannot be made).
$(DLL): $(OBJS) $(LIBS) | $(OUT)/Bin
	$(LLD) $(LDFLAGS) /out:$@ $(OBJS) $(LIBS)
	@asm=$$($(OBJDUMP) -d --no-show-raw-insn $@) || exit 1; \
	if printf '%s\n' "$$asm" | grep -qiE 'subl?[[:space:]]+\$$0x[0-9a-f]{4,},[[:space:]]*%esp'; then \
	  echo "error: stack frame of 4 KiB or more in $@" >&2; exit 1; fi

$(INI): dist/Bin/SS2CoopLobby.ini | $(OUT)/Bin
	cp $< $@

$(MOD): dist/Content/SeriousSam2.mod | $(OUT)/Content
	cp $< $@

$(OUT)/Bin $(OUT)/Content $(OBJ):
	mkdir -p $@

clean:
	rm -rf $(OUT)
