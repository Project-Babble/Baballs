// frame_buffer.h
#ifndef FRAME_BUFFER_H
#define FRAME_BUFFER_H

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

// Forward declaration - use the exact same struct name from jpeg_stream.h
struct MJPEGStream;

class FrameBuffer {
public:
    // New constructor with target resolution
    FrameBuffer(const char* url, int targetWidth, int targetHeight, int updateInterval = 30);
    FrameBuffer(const char* url, int updateInterval = 30);
    FrameBuffer(int targetWidth, int targetHeight, int updateInterval = 30);
    ~FrameBuffer();

    // Get a copy of the current frame
    // The caller is responsible for freeing the returned memory
    unsigned char* getFrameCopy(int* width, int* height, uint64_t* time, size_t* data_size);

    void setTargetResolution(int width, int height);
    
    // Get direct access to the current frame buffer (no copy)
    //void lockFrame(int** pixels, int* width, int* height);
    //void unlockFrame();
    void setURL(const char* url);
    
    // Control methods
    void start();
    void stop();
    bool isRunning() const;

private:
    // Frame data structure
    struct Frame {
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        uint64_t time = 0;
        size_t frame_size = 0;
        
        void clear() {
            if (pixels) {
                free(pixels);
                pixels = nullptr;
            }
            width = height = 0;
        }
    };

    // Buffer update thread function
    void updateLoop();

    // Swap front and back buffers
    void swapBuffers();

    int* resizeFrame(int* sourcePixels, int sourceWidth, int sourceHeight, 
        int targetWidth, int targetHeight);

    // Original stream
    MJPEGStream* stream = nullptr;
    const char* streamUrl;
    
    // Double buffer
    Frame buffers[2];
    int frontBuffer; // Index of the current front buffer
    
    // Thread control
    std::atomic<bool> running;
    std::thread updateThread;
    int updateIntervalMs;
    
    // Synchronization
    std::mutex frameMutex;
    std::condition_variable frameCondition;
    bool frameInUse;
    int targetWidth = 0;
    int targetHeight = 0;
    bool resizeEnabled = false;
};

#endif // FRAME_BUFFER_H