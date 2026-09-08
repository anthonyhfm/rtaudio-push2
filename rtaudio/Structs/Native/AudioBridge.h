//
//  AudioBridge.h
//  rtaudio
//
//  Created by zeph on 10/03/26.
//

#import <Foundation/Foundation.h>
#import <simd/simd.h>

NS_ASSUME_NONNULL_BEGIN

@interface AudioBridge : NSObject

- (void)processBuffer:(const float *)buffer count:(int)count;

// Reads atomically from the processor — safe to call from any thread
- (simd_float4)getSmoothedMagnitudes;

// Push 2 display output. Connection attempts continue in the background so
// the controller can be plugged in after rtaudio has launched.
- (void)startPush2Display;
- (void)stopPush2Display;
- (BOOL)isPush2DisplayConnected;
- (void)setPush2DisplayColorTop:(simd_float3)top bottom:(simd_float3)bottom;

@end

NS_ASSUME_NONNULL_END
