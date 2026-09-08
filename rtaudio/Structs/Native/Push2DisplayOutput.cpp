#include "Push2DisplayOutput.hpp"

// Push 2 transport and pixel-format details are based on Ableton AG's
// push2-display-with-juce reference implementation (MIT License):
// Copyright (c) 2017 Ableton AG, Berlin.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the MIT License's conditions.

#include <libusb.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace {
constexpr uint16_t kAbletonVendorID = 0x2982;
constexpr uint16_t kPush2ProductID = 0x1967;
constexpr unsigned char kBulkEndpointOut = 0x01;
constexpr int kUsbInterface = 0;
constexpr int kLinesPerTransfer = 8;
constexpr int kTransferTimeoutMs = 250;
constexpr unsigned char kFrameHeader[16] = {
    0xff, 0xcc, 0xaa, 0x88, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
}

Push2DisplayOutput::Push2DisplayOutput(const AudioProcessor& processor)
    : processor_(processor) {
    colors_[0].store(1.0f); colors_[1].store(1.0f); colors_[2].store(1.0f);
    colors_[3].store(0.45f); colors_[4].store(0.45f); colors_[5].store(0.45f);
}

Push2DisplayOutput::~Push2DisplayOutput() {
    stop();
}

void Push2DisplayOutput::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) return;
    worker_ = std::thread(&Push2DisplayOutput::run, this);
}

void Push2DisplayOutput::stop() {
    if (!running_.exchange(false)) return;
    waitCondition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool Push2DisplayOutput::isConnected() const {
    return connected_.load(std::memory_order_relaxed);
}

void Push2DisplayOutput::setColors(float topR, float topG, float topB,
                                   float bottomR, float bottomG, float bottomB) {
    const float values[6] = {topR, topG, topB, bottomR, bottomG, bottomB};
    for (int i = 0; i < 6; ++i)
        colors_[i].store(std::clamp(values[i], 0.0f, 1.0f), std::memory_order_relaxed);
}

void Push2DisplayOutput::run() {
    using namespace std::chrono_literals;
    auto nextFrame = std::chrono::steady_clock::now();

    while (running_.load(std::memory_order_relaxed)) {
        if (!device_) {
            if (!connect()) {
                std::unique_lock lock(waitMutex_);
                waitCondition_.wait_for(lock, 1s, [this] { return !running_.load(); });
                continue;
            }
            nextFrame = std::chrono::steady_clock::now();
        }

        renderFrame();
        if (!sendFrame()) {
            disconnect();
            continue;
        }

        nextFrame += 33ms;
        std::unique_lock lock(waitMutex_);
        waitCondition_.wait_until(lock, nextFrame, [this] { return !running_.load(); });
        if (nextFrame < std::chrono::steady_clock::now() - 100ms)
            nextFrame = std::chrono::steady_clock::now();
    }

    disconnect();
}

bool Push2DisplayOutput::connect() {
    if (libusb_init(&usbContext_) < 0) {
        usbContext_ = nullptr;
        return false;
    }

    device_ = libusb_open_device_with_vid_pid(usbContext_, kAbletonVendorID, kPush2ProductID);
    if (!device_) {
        libusb_exit(usbContext_);
        usbContext_ = nullptr;
        return false;
    }

    const int claimResult = libusb_claim_interface(device_, kUsbInterface);
    if (claimResult < 0) {
        std::fprintf(stderr, "Push 2 display: USB interface unavailable (%s)\n",
                     libusb_error_name(claimResult));
        disconnect();
        return false;
    }

    connected_.store(true, std::memory_order_relaxed);
    std::fprintf(stderr, "Push 2 display connected\n");
    return true;
}

void Push2DisplayOutput::disconnect() {
    connected_.store(false, std::memory_order_relaxed);
    if (device_) {
        libusb_release_interface(device_, kUsbInterface);
        libusb_close(device_);
        device_ = nullptr;
    }
    if (usbContext_) {
        libusb_exit(usbContext_);
        usbContext_ = nullptr;
    }
}

bool Push2DisplayOutput::sendFrame() {
    int transferred = 0;
    auto* header = const_cast<unsigned char*>(kFrameHeader);
    int result = libusb_bulk_transfer(device_, kBulkEndpointOut, header,
                                      sizeof(kFrameHeader), &transferred, kTransferTimeoutMs);
    if (result < 0 || transferred != sizeof(kFrameHeader)) return false;

    constexpr int bytesPerLine = kStridePixels * static_cast<int>(sizeof(uint16_t));
    constexpr int bytesPerTransfer = bytesPerLine * kLinesPerTransfer;
    auto* bytes = reinterpret_cast<unsigned char*>(frame_.data());

    for (int line = 0; line < kHeight; line += kLinesPerTransfer) {
        transferred = 0;
        result = libusb_bulk_transfer(device_, kBulkEndpointOut,
                                      bytes + line * bytesPerLine, bytesPerTransfer,
                                      &transferred, kTransferTimeoutMs);
        if (result < 0 || transferred != bytesPerTransfer) return false;
    }
    return true;
}

void Push2DisplayOutput::renderFrame() {
    // The 64 padding pixels at the end of every line stay zero, matching Ableton's
    // documented 2048-byte line stride. Visible pixels require alternating masks.
    frame_.fill(0);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kVisibleWidth; ++x)
            frame_[y * kStridePixels + x] = (x & 1) ? 0xffe7 : 0xf3e7;

    std::array<float, AudioProcessor::waveformSampleCount()> samples{};
    float peak = 0.0f;
    for (int i = 0; i < static_cast<int>(samples.size()); ++i) {
        samples[i] = processor_.getWaveformSample(i);
        if (!std::isfinite(samples[i])) samples[i] = 0.0f;
        peak = std::max(peak, std::abs(samples[i]));
    }

    const float targetGain = std::clamp(0.82f / std::max(peak, 0.015f), 1.0f, 10.0f);
    automaticGain_ += (targetGain - automaticGain_) * (targetGain < automaticGain_ ? 0.35f : 0.06f);

    // Quiet center line gives the display a stable visual reference.
    const float bottomR = colors_[3].load(std::memory_order_relaxed);
    const float bottomG = colors_[4].load(std::memory_order_relaxed);
    const float bottomB = colors_[5].load(std::memory_order_relaxed);
    const auto dim = [](float value) { return static_cast<uint8_t>(std::clamp(value * 30.0f, 0.0f, 255.0f)); };
    for (int x = 12; x < kVisibleWidth - 12; ++x)
        setPixel(x, kHeight / 2, dim(bottomR), dim(bottomG), dim(bottomB));

    for (int i = 1; i < static_cast<int>(samples.size()); ++i) {
        const int x0 = (i - 1) * (kVisibleWidth - 25) / (static_cast<int>(samples.size()) - 1) + 12;
        const int x1 = i * (kVisibleWidth - 25) / (static_cast<int>(samples.size()) - 1) + 12;
        const float s0 = std::clamp(samples[i - 1] * automaticGain_, -1.0f, 1.0f);
        const float s1 = std::clamp(samples[i] * automaticGain_, -1.0f, 1.0f);
        const int y0 = static_cast<int>((kHeight - 1) * (0.5f - s0 * 0.43f));
        const int y1 = static_cast<int>((kHeight - 1) * (0.5f - s1 * 0.43f));
        drawLine(x0, y0, x1, y1, 2);
    }
}

void Push2DisplayOutput::drawLine(int x0, int y0, int x1, int y1, int thickness) {
    const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        const float t = 1.0f - static_cast<float>(std::clamp(y0, 0, kHeight - 1)) / (kHeight - 1);
        const float r = colors_[3].load(std::memory_order_relaxed) * (1.0f - t) + colors_[0].load(std::memory_order_relaxed) * t;
        const float g = colors_[4].load(std::memory_order_relaxed) * (1.0f - t) + colors_[1].load(std::memory_order_relaxed) * t;
        const float b = colors_[5].load(std::memory_order_relaxed) * (1.0f - t) + colors_[2].load(std::memory_order_relaxed) * t;
        for (int oy = -thickness; oy <= thickness; ++oy)
            for (int ox = -thickness; ox <= thickness; ++ox)
                if (ox * ox + oy * oy <= thickness * thickness)
                    setPixel(x0 + ox, y0 + oy,
                             static_cast<uint8_t>(r * 255.0f),
                             static_cast<uint8_t>(g * 255.0f),
                             static_cast<uint8_t>(b * 255.0f));

        if (x0 == x1 && y0 == y1) break;
        const int twiceError = 2 * error;
        if (twiceError >= dy) { error += dy; x0 += sx; }
        if (twiceError <= dx) { error += dx; y0 += sy; }
    }
}

void Push2DisplayOutput::setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    if (x < 0 || x >= kVisibleWidth || y < 0 || y >= kHeight) return;
    const uint16_t mask = (x & 1) ? 0xffe7 : 0xf3e7;
    frame_[y * kStridePixels + x] = pushPixel(r, g, b) ^ mask;
}

uint16_t Push2DisplayOutput::pushPixel(uint8_t r, uint8_t g, uint8_t b) {
    // Push 2 uses the BGR565 bit ordering documented by Ableton.
    return static_cast<uint16_t>(((b & 0xf8) >> 3) << 11 |
                                 ((g & 0xfc) >> 2) << 5 |
                                 ((r & 0xf8) >> 3));
}
