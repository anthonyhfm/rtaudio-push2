#pragma once

#include "AudioProcessor.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

struct libusb_context;
struct libusb_device_handle;

class Push2DisplayOutput {
public:
    static constexpr int kVisibleWidth = 960;
    static constexpr int kHeight = 160;
    static constexpr int kStridePixels = 1024;

    explicit Push2DisplayOutput(const AudioProcessor& processor);
    ~Push2DisplayOutput();

    Push2DisplayOutput(const Push2DisplayOutput&) = delete;
    Push2DisplayOutput& operator=(const Push2DisplayOutput&) = delete;

    void start();
    void stop();
    bool isConnected() const;
    void setColors(float topR, float topG, float topB,
                   float bottomR, float bottomG, float bottomB);

private:
    void run();
    bool connect();
    void disconnect();
    bool sendFrame();
    void renderFrame();
    void setPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b);
    void drawLine(int x0, int y0, int x1, int y1, int thickness);
    static uint16_t pushPixel(uint8_t r, uint8_t g, uint8_t b);

    const AudioProcessor& processor_;
    std::array<uint16_t, kStridePixels * kHeight> frame_{};
    std::array<std::atomic<float>, 6> colors_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::thread worker_;
    std::mutex waitMutex_;
    std::condition_variable waitCondition_;
    libusb_context* usbContext_ = nullptr;
    libusb_device_handle* device_ = nullptr;
    float automaticGain_ = 1.0f;
};
