#include "system_audio_capture.hpp"

#ifdef __APPLE__

#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>

#include <atomic>
#include <mutex>

#include "log.hpp"

// SCStreamOutput delegate that forwards audio samples to the C++ callback
@interface DRAudioStreamOutput : NSObject <SCStreamOutput>
@property (nonatomic, assign) SystemAudioCapture::AudioCallback callback;
@end

@implementation DRAudioStreamOutput

- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
                   ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeAudio) {
        return;
    }
    if (!_callback) {
        return;
    }

    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    if (!blockBuffer) {
        return;
    }

    size_t totalLength = 0;
    char *dataPointer = nullptr;
    OSStatus status = CMBlockBufferGetDataPointer(blockBuffer, 0, nullptr, &totalLength, &dataPointer);
    if (status != kCMBlockBufferNoErr || !dataPointer || totalLength == 0) {
        return;
    }

    const CMFormatDescriptionRef formatDesc = CMSampleBufferGetFormatDescription(sampleBuffer);
    if (!formatDesc) {
        return;
    }

    const AudioStreamBasicDescription *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(formatDesc);
    if (!asbd) {
        return;
    }

    int channels = static_cast<int>(asbd->mChannelsPerFrame);
    size_t frames = totalLength / (sizeof(float) * channels);

    _callback(reinterpret_cast<const float *>(dataPointer), frames, channels);
}

@end

class SystemAudioCaptureMac : public SystemAudioCapture {
public:
    ~SystemAudioCaptureMac() override { stop(); }

    bool start(AudioCallback cb) override {
        if (running_) {
            return true;
        }

        callback_ = std::move(cb);

        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        __block bool success = false;

        [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *error) {
            if (error || !content) {
                LOG_ERROR() << "SCShareableContent error: "
                            << (error ? [[error localizedDescription] UTF8String] : "nil");
                dispatch_semaphore_signal(sem);
                return;
            }

            // Exclude our own application from capture
            pid_t myPid = [[NSProcessInfo processInfo] processIdentifier];
            NSMutableArray<SCRunningApplication *> *excludedApps = [NSMutableArray array];
            for (SCRunningApplication *app in content.applications) {
                if (app.processID == myPid) {
                    [excludedApps addObject:app];
                    break;
                }
            }

            SCContentFilter *filter = [[SCContentFilter alloc]
                initWithDisplay:content.displays.firstObject
                excludingApplications:excludedApps
                exceptingWindows:@[]];

            SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
            config.capturesAudio = YES;
            config.excludesCurrentProcessAudio = YES;
            config.sampleRate = SAMPLE_RATE;
            config.channelCount = CHANNELS;

            // We only need audio, minimize video overhead
            config.width = 2;
            config.height = 2;
            config.minimumFrameInterval = CMTimeMake(1, 1);

            self->stream_ = [[SCStream alloc] initWithFilter:filter configuration:config delegate:nil];
            self->output_ = [[DRAudioStreamOutput alloc] init];
            self->output_.callback = self->callback_;

            NSError *addErr = nil;
            [self->stream_ addStreamOutput:self->output_
                                      type:SCStreamOutputTypeAudio
                        sampleHandlerQueue:dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0)
                                     error:&addErr];
            if (addErr) {
                LOG_ERROR() << "addStreamOutput error: " << [[addErr localizedDescription] UTF8String];
                dispatch_semaphore_signal(sem);
                return;
            }

            [self->stream_ startCaptureWithCompletionHandler:^(NSError *startErr) {
                if (startErr) {
                    LOG_ERROR() << "startCapture error: " << [[startErr localizedDescription] UTF8String];
                } else {
                    success = true;
                }
                dispatch_semaphore_signal(sem);
            }];
        }];

        dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));

        if (success) {
            running_ = true;
        }
        return success;
    }

    void stop() override {
        if (!running_) {
            return;
        }
        running_ = false;

        if (stream_) {
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [stream_ stopCaptureWithCompletionHandler:^(NSError *) {
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC));
            stream_ = nil;
        }
        output_ = nil;
    }

    bool running() const override { return running_; }

private:
    AudioCallback callback_;
    std::atomic<bool> running_{false};
    SCStream *stream_ = nil;
    DRAudioStreamOutput *output_ = nil;
};

bool SystemAudioCapture::available() {
    if (@available(macOS 13.0, *)) {
        return true;
    }
    return false;
}

std::unique_ptr<SystemAudioCapture> SystemAudioCapture::create() {
    if (!available()) {
        return nullptr;
    }
    return std::make_unique<SystemAudioCaptureMac>();
}

#endif // __APPLE__
