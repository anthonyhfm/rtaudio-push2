// Build the libusb sources in one translation unit, following Ableton AG's
// push2-display-with-juce macOS project (Copyright 2017, MIT License).
// libusb itself is an external LGPL-2.1 submodule.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#include "../../../../ThirdParty/libusb/libusb/core.c"
#include "../../../../ThirdParty/libusb/libusb/descriptor.c"
#include "../../../../ThirdParty/libusb/libusb/hotplug.c"
#include "../../../../ThirdParty/libusb/libusb/io.c"
#include "../../../../ThirdParty/libusb/libusb/strerror.c"
#include "../../../../ThirdParty/libusb/libusb/sync.c"
#include "../../../../ThirdParty/libusb/libusb/os/darwin_usb.c"
#include "../../../../ThirdParty/libusb/libusb/os/poll_posix.c"
#include "../../../../ThirdParty/libusb/libusb/os/threads_posix.c"
#pragma clang diagnostic pop
