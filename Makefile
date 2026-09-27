NAME   := RDMAEnabler
BUNDLE := build/$(NAME).kext
BIN    := $(BUNDLE)/Contents/MacOS/$(NAME)

SDK    := $(shell xcrun --show-sdk-path)

CXXFLAGS := -arch arm64e -isysroot $(SDK) \
	-nostdinc -I$(SDK)/System/Library/Frameworks/Kernel.framework/Headers \
	-mkernel -fapple-kext -fno-builtin -fno-common -fno-exceptions -fno-rtti \
	-DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
	-std=c++17 -O2 -Wall
LDFLAGS  := -nostdlib -Xlinker -kext -lkmodc++ -lkmod -lcc_kext

.PHONY: all
all: $(BIN)

.PHONY: install
install: $(BIN)
	sudo rm -rf /Library/Extensions/$(NAME).kext
	sudo cp -R $(BUNDLE) /Library/Extensions/
	sudo chown -R root:wheel /Library/Extensions/$(NAME).kext

.PHONY: clean
clean:
	rm -rf build

$(BIN): $(NAME).cpp Info.plist
	mkdir -p $(@D)
	xcrun clang++ $(CXXFLAGS) $(LDFLAGS) -o $@ $<
	cp Info.plist $(BUNDLE)/Contents/Info.plist
	codesign -f -s - $(BUNDLE)
