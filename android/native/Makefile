# libemerald.so: origin's 3ds_port/Makefile, unchanged, built for Android.
#
# Runs with 3ds_port as the working directory, like origin's own build, since
# origin's rules use paths relative to it (src/, build/, romfs/, ../tools):
#
#   make -C build/upstream/3ds_port -f ../android/native/Makefile -j4
#
# The devkitPro names origin calls (arm-none-eabi-*, picasso) are generated
# wrappers (android/toolchain/gen_devkitpro.py). Only the two links change:
# both produce a shared object linked by GNU ld against the NDK sysroot with
# android.ld.in, so the maps, symbols and section layout origin's scripts read
# are the same kind as on the console.

ANDROID_NATIVE := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ANDROID_DIR := $(abspath $(ANDROID_NATIVE)/..)

# Origin pipes its C preprocessor into the text preprocessor and compiler.
# A failure anywhere in that pipeline (or in the link/tee pipeline) must
# fail the build rather than leaving an apparently successful empty object.
SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c

NDK_VERSION := 27.2.12479018
ANDROID_API := 28
SDK_DIR := $(firstword $(ANDROID_HOME) $(ANDROID_SDK_ROOT) $(HOME)/Android/Sdk)
NDK ?= $(firstword $(ANDROID_NDK_HOME) $(wildcard $(SDK_DIR)/ndk/$(NDK_VERSION)))
ifeq ($(wildcard $(NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/clang),)
$(error Android NDK $(NDK_VERSION) not found: set NDK=, ANDROID_NDK_HOME or ANDROID_HOME)
endif
NDK_LLVM := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64
NDK_LIB := $(NDK_LLVM)/sysroot/usr/lib/arm-linux-androideabi/$(ANDROID_API)
NDK_BUILTINS := $(firstword $(wildcard $(NDK_LLVM)/lib/clang/*/lib/linux/libclang_rt.builtins-arm-android.a))
NDK_UNWIND := $(firstword $(wildcard $(NDK_LLVM)/lib/clang/*/lib/linux/arm/libunwind.a))

PYTHON ?= python3
# Where `package` puts the library and the RomFS for Gradle (-Pemerald.nativeOut).
ANDROID_OUT ?= $(abspath ../../android-out)
# The address the library is linked at, and must be loaded at (boot/).
LOAD_ADDRESS ?= 0x04000000
# The fake devkitPro tree, regenerated (cheaply, only changed files are
# written) on every run so a moved NDK or a new picasso2glsl.py is picked up.
ANDROID_TOOLCHAIN := $(ANDROID_NATIVE)/build/devkitpro
# tools/ of this repository, mirrored there by tools/bootstrap.py.
PICASSO2GLSL := $(ANDROID_DIR)/tools/picasso2glsl.py
TOOLCHAIN_STATUS := $(shell "$(PYTHON)" "$(ANDROID_DIR)/toolchain/gen_devkitpro.py" \
	--out "$(ANDROID_TOOLCHAIN)" --ndk "$(NDK)" --android-dir "$(ANDROID_DIR)" \
	--picasso "$(PICASSO2GLSL)" --python "$(PYTHON)" >&2; echo $$?)
ifneq ($(TOOLCHAIN_STATUS),0)
$(error android/toolchain/gen_devkitpro.py failed)
endif

override DEVKITPRO := $(ANDROID_TOOLCHAIN)
override DEVKITARM := $(ANDROID_TOOLCHAIN)/devkitARM
# Origin's scripts find arm-none-eabi-{nm,readelf,objdump,objcopy} through it.
export DEVKITPRO DEVKITARM

include Makefile

# Android-only engine hooks are kept outside the immutable origin subtree.
CPPFLAGS += -I$(ANDROID_NATIVE)/include
FULLFLAGS += -I$(ANDROID_NATIVE)/include

.DEFAULT_GOAL := android

# Compiler differences (GCC on the console, clang here) that are handled by
# flags; FULLCFLAGS is the game units' set (full.mk), CFLAGS the native one.
# Bionic defines POSIX LINE_MAX; origin uses this name for its scanline
# buffer width. The local redefinition is intentional and has no ABI effect.
build/3ds_video.o: CFLAGS += -Wno-error=macro-redefined
build/root/src/music_player.o: FULLCFLAGS += -include $(ANDROID_NATIVE)/compat/music_player.h
build/root/src/music_player.o: $(ANDROID_NATIVE)/compat/music_player.h

# The voxel and intro generators read the decomp's converted art (tiles.4bpp,
# palettes/*.gbapal, graphics/intro/scene_1/bg.4bpp) and quietly produce
# different output without it. Origin gets those files as a side effect of
# gen_asset_table.py (after the link) in a serial build; in a parallel build
# from a fresh tree nothing orders them, so the root Makefile's own rules are
# run first here.
TILESET_ART := $(patsubst %.png,%.4bpp,$(wildcard $(ROOT)/data/tilesets/*/*/tiles.png))
TILESET_ART += $(patsubst %.pal,%.gbapal,$(wildcard $(ROOT)/data/tilesets/*/*/palettes/*.pal))
build/android/tileset_art.stamp: $(wildcard $(ROOT)/data/tilesets/*/*/tiles.png $(ROOT)/data/tilesets/*/*/palettes/*.pal)
	+$(MAKE) -C $(ROOT) $(patsubst $(ROOT)/%,%,$(TILESET_ART))
	@mkdir -p $(dir $@)
	@touch $@
$(ROMFS_SHADER_OUTS): build/android/tileset_art.stamp
$(ROOT)/graphics/%.4bpp: $(ROOT)/graphics/%.png
	+$(MAKE) -C $(ROOT) $(patsubst $(ROOT)/%,%,$@)

# The bottom UI opens this tilemap by filename. Clang removes the unused
# game INCBIN symbol, so origin's symbol-driven asset exporter cannot see it.
# Stage the source bytes explicitly, before hashing the complete data ABI.
ANDROID_DIRECT_ASSETS := romfs/graphics/party_menu/slot_wide_empty.bin
$(ANDROID_DIRECT_ASSETS): romfs/%: $(ROOT)/%
	@mkdir -p $(dir $@)
	cp $< $@
ROMFS_DATA_OUTS += $(ANDROID_DIRECT_ASSETS)
romfs/engine/abi.bin: $(ANDROID_DIRECT_ASSETS)

# ── The Android layer: shim (libctru), GPU (citro3d/citro2d), host (JNI) ─────
ANDROID_SRCS := $(wildcard $(ANDROID_DIR)/shim/src/*.c $(ANDROID_DIR)/shim/src/*/*.c $(ANDROID_DIR)/shim/src/*/*/*.c)
ANDROID_SRCS += $(wildcard $(ANDROID_DIR)/gpu/src/*.c $(ANDROID_DIR)/gpu/src/*/*.c $(ANDROID_DIR)/gpu/src/*/*/*.c)
ANDROID_SRCS += $(wildcard $(ANDROID_DIR)/host/src/*.c $(ANDROID_DIR)/host/src/*/*.c $(ANDROID_DIR)/host/src/*/*/*.c)
ANDROID_SRCS += $(ANDROID_NATIVE)/src/load_address.c $(ANDROID_NATIVE)/src/save_abi.c
ANDROID_SRCS += $(ANDROID_NATIVE)/src/qol_rules.c $(ANDROID_NATIVE)/src/qol_game.c $(ANDROID_NATIVE)/src/qol_backups.c
ANDROID_SRCS += $(ANDROID_NATIVE)/src/mystery_events.c
ANDROID_OBJS := $(patsubst $(ANDROID_DIR)/%.c,build/android/%.o,$(ANDROID_SRCS))
ANDROID_CFLAGS := -O2 -g -std=gnu11 -Wall -Wextra -ffunction-sections -fdata-sections \
	-DCTR_LOAD_ADDRESS=$(LOAD_ADDRESS)

$(ANDROID_OBJS): build/config
build/android/%.o: $(ANDROID_DIR)/%.c
	@mkdir -p $(dir $@)
	"$(CC)" $(CPPFLAGS) $(ANDROID_CFLAGS) -MMD -MP -c $< -o $@

# Game hooks and save compatibility assertions use the actual game ABI.
ANDROID_GAME_OBJS := build/android/native/src/save_abi.o build/android/native/src/qol_game.o build/android/native/src/mystery_events.o
$(ANDROID_GAME_OBJS): build/android/native/%.o: $(ANDROID_NATIVE)/%.c
	@mkdir -p $(dir $@)
	"$(CC)" $(FULLFLAGS) $(FULLCFLAGS) -std=gnu11 -MMD -MP -c $< -o $@

-include $(ANDROID_OBJS:.o=.d)

# Origin's game preprocessor pipeline does not emit C dependency files.
# Header changes in a new upstream revision must invalidate its C objects.
GAME_HEADERS := $(shell find $(ROOT)/include include compat -type f -name '*.h')
GAME_HEADERS += $(wildcard $(ANDROID_NATIVE)/include/*.h)
$(FULL_C_OBJS): $(GAME_HEADERS)
$(OBJECTS) $(ANDROID_OBJS): $(ANDROID_NATIVE)/Makefile $(CC)

# ── The link ─────────────────────────────────────────────────────────────────
LD_BFD := arm-linux-gnueabi-ld.bfd
# libc file functions the shim redirects (romfs:/, sdmc:/), one per line.
WRAP_SYMBOLS := $(strip $(shell cat "$(ANDROID_DIR)/shim/wrap.txt" 2>/dev/null | sed 's/\#.*//'))
ANDROID_LIBS := -lc -lm -ldl -llog -landroid -lEGL -lGLESv3 -laaudio
# -pie, not -shared: see elf_dyn.py. Only the JNI entry points are exported.
ANDROID_LDFLAGS := -m armelf_linux_eabi -pie --no-dynamic-linker -e 0 -soname libemerald.so \
	--export-dynamic-symbol=JNI_OnLoad --export-dynamic-symbol=JNI_OnUnload --export-dynamic-symbol='Java_*' \
	-z noexecstack -z relro -z now -z max-page-size=4096 -z common-page-size=4096 --hash-style=both \
	--build-id=sha1 --eh-frame-hdr --gc-sections --emit-relocs --use-blx --undefined=__ctr_script_keep \
	$(addprefix --wrap=,$(WRAP_SYMBOLS)) \
	-L$(NDK_LIB)
LINK_OBJECTS = $(NDK_LIB)/crtbegin_so.o $(OBJECTS) $(ANDROID_OBJS) \
	$(NDK_BUILTINS) $(NDK_UNWIND) $(ANDROID_LIBS) $(NDK_BUILTINS) $(NDK_LIB)/crtend_so.o

build/android_%.ld: $(ANDROID_NATIVE)/android.ld.in
	@mkdir -p build
	sed -e 's/@LOAD_ADDRESS@/$(LOAD_ADDRESS)/' -e 's/@GAMEDATA_TYPE@/$(if $(filter release,$*),(NOLOAD),)/' $< > $@

# Same objects, same order, same script apart from @GAMEDATA_TYPE@: the two
# links must agree on every address (gen_gamedata_bundle.py checks).
$(TARGET).elf: $(OBJECTS) $(ANDROID_OBJS) build/android_release.ld build/android.wrap $(ANDROID_NATIVE)/elf_dyn.py
	@mkdir -p build
	$(file >build/link.rsp,$(LINK_OBJECTS))
	$(LD_BFD) $(ANDROID_LDFLAGS) -T build/android_release.ld -Map build/$(TARGET).map @build/link.rsp -o $@ \
		2>&1 | tee build/link.log
	"$(PYTHON)" $(ANDROID_NATIVE)/elf_dyn.py $@

build/gamedata_image.elf: $(OBJECTS) $(ANDROID_OBJS) build/android_image.ld build/android.wrap $(ANDROID_NATIVE)/elf_dyn.py
	@mkdir -p build
	$(file >build/link_image.rsp,$(LINK_OBJECTS))
	$(LD_BFD) $(ANDROID_LDFLAGS) -T build/android_image.ld -Map build/gamedata_image.map @build/link_image.rsp \
		-o $@ 2>build/link_image.log || { cat build/link_image.log; exit 1; }
	"$(PYTHON)" $(ANDROID_NATIVE)/elf_dyn.py $@

# The wrap list is a link input: relink when it changes.
build/android.wrap: FORCE
	@mkdir -p build
	@printf '%s\n' $(WRAP_SYMBOLS) > $@.new
	@cmp -s $@.new $@ || cp $@.new $@
	@rm -f $@.new

# A new picasso (the placeholder replaced by tools/picasso2glsl.py) rebuilds the shader.
romfs/shaders/voxel.shbin: $(DEVKITPRO)/tools/bin/picasso $(wildcard $(PICASSO2GLSL))

# What ships: the release link without its link-time relocation sections
# (.rel.gamedata among them, as origin's objcopy does for the 3DSX) and
# without DWARF. The symbol table stays, for crash reports.
build/android/libemerald.so: $(TARGET).elf build/gamedata_image.elf $(ANDROID_NATIVE)/check_link.py
	@mkdir -p $(dir $@)
	"$(PYTHON)" $(ANDROID_NATIVE)/check_link.py --elf $(TARGET).elf --image build/gamedata_image.elf \
		--load-address $(LOAD_ADDRESS) --ndk-lib $(NDK_LIB) $(addprefix --lib=,$(ANDROID_LIBS)) \
		--link-log build/link.log --undefined-out build/undefined.txt
	arm-linux-gnueabi-objcopy --strip-debug --remove-relocations='*' $< $@

# The loader that maps libemerald.so at its link address (see boot/).
build/android/boot_span.h: $(TARGET).elf
	@mkdir -p $(dir $@)
	"$(PYTHON)" $(ANDROID_NATIVE)/check_link.py --elf $< --span-header $@

build/android/libemeraldboot.so: $(ANDROID_NATIVE)/boot/emerald_boot.c build/android/boot_span.h
	"$(CC)" -O2 -std=gnu11 -Wall -Wextra -Werror -shared -fuse-ld=lld -Wl,-soname,libemeraldboot.so \
		-Wl,--no-undefined -Wl,-z,noexecstack -Wl,--build-id=sha1 -Ibuild/android $< -ldl -llog -o $@

ANDROID_ROMFS := $(ROMFS_DATA_OUTS) romfs/engine/abi.bin romfs/boot.txt $(EMBED_MARKER)

.PHONY: android package android-release package-release
android: build/android/libemerald.so build/android/libemeraldboot.so $(ANDROID_ROMFS)

# Gradle reads $(ANDROID_OUT)/jniLibs/<abi>/*.so and $(ANDROID_OUT)/assets/romfs/.
package: android
	@mkdir -p $(ANDROID_OUT)/jniLibs/armeabi-v7a $(ANDROID_OUT)/assets
	cp build/android/libemerald.so build/android/libemeraldboot.so $(ANDROID_OUT)/jniLibs/armeabi-v7a/
	rm -rf $(ANDROID_OUT)/assets/romfs
	cp -R romfs $(ANDROID_OUT)/assets/romfs

# Origin's release: the engine files only; the game data comes from the data
# pack the player builds from their own ROM.
android-release: build/android/libemerald.so build/android/libemeraldboot.so romfs/boot.txt \
		$(ROMFS_DATA_OUTS) romfs/engine/abi.bin
	"$(PYTHON)" $(SCRIPTS)/staging.py engine --romfs romfs --out build/romfs_release

package-release: android-release
	@mkdir -p $(ANDROID_OUT)/jniLibs/armeabi-v7a $(ANDROID_OUT)/assets
	cp build/android/libemerald.so build/android/libemeraldboot.so $(ANDROID_OUT)/jniLibs/armeabi-v7a/
	rm -rf $(ANDROID_OUT)/assets/romfs
	cp -R build/romfs_release $(ANDROID_OUT)/assets/romfs
