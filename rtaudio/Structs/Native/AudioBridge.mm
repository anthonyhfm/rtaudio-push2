//
//  AudioBridge.mm
//  rtaudio
//
//  Created by zeph on 10/03/26.
//


#import "AudioBridge.h"
#import "AudioProcessor.hpp"
#import "Push2DisplayOutput.hpp"

@implementation AudioBridge {
    AudioProcessor *processor;
    Push2DisplayOutput *push2Display;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        processor = new AudioProcessor();
        push2Display = new Push2DisplayOutput(*processor);
    }
    return self;
}

- (void)startPush2Display {
    push2Display->start();
}

- (void)stopPush2Display {
    push2Display->stop();
}

- (BOOL)isPush2DisplayConnected {
    return push2Display->isConnected();
}

- (void)setPush2DisplayColorTop:(simd_float3)top bottom:(simd_float3)bottom {
    push2Display->setColors(top.x, top.y, top.z, bottom.x, bottom.y, bottom.z);
}

- (void)processBuffer:(const float *)buffer count:(int)count {
    processor->process(buffer, count);
}

- (simd_float4)getSmoothedMagnitudes {
    // Calls getBand() which does memory_order_relaxed atomic loads —
    // no heap allocation, safe to call from the render thread
    return simd_make_float4(
        processor->getBand(0),
        processor->getBand(1),
        processor->getBand(2),
        processor->getBand(3)
    );
}

- (void)dealloc {
    delete push2Display;
    delete processor;
}

@end
