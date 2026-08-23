.PHONY: build-release

CMAKE ?= cmake
TARGET ?=
TARGET_NAME ?=
BUILD_DIR ?= build/$(TARGET)
RELEASE_DIR ?= build/releases
CMAKE_ARGS ?=
EXECUTABLE_SUFFIX ?=
BUILD_JOBS ?= 2

build-release:
	test -n "$(TARGET)"
	test -n "$(TARGET_NAME)"
	$(CMAKE) -S . -B $(BUILD_DIR) -DREMOTIFY_TARGET=$(TARGET) -DCMAKE_BUILD_TYPE=Release $(CMAKE_ARGS)
	$(CMAKE) --build $(BUILD_DIR) --target remotifyd -j$(BUILD_JOBS)
	mkdir -p $(RELEASE_DIR)
	cp $(BUILD_DIR)/remotifyd$(EXECUTABLE_SUFFIX) $(RELEASE_DIR)/$(TARGET_NAME)
