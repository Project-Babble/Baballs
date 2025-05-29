#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>  // MUST come first
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#endif

#include <chrono>
#include <inttypes.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#ifdef _WIN32
    #include <conio.h>
    #include <windows.h>
#else
    #include <sys/time.h>
    #include <time.h>
#endif
#include <openvr/openvr.h>


#include "capture_data.h"
#include "config.h"
#include "dashboard_ui.h"
#include "flags.h"
#include "frame_buffer.h"
#include "math_utils.h"
#include "numpy_io.h"
#include "overlay_manager.h"
#include "path.h"
#include "rest_server.h"
#include "trainer_wrapper.h"
#include <turbojpeg.h>
#include <onnxruntime/onnxruntime_cxx_api.h>
#include <fstream>
#include <memory>


#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE FileHandle;
    #define INVALID_HANDLE INVALID_HANDLE_VALUE
#else
    #include <unistd.h>
    #include <fcntl.h>
    typedef int FileHandle;
    #define INVALID_HANDLE -1
#endif



FileHandle openCaptureFile(const char* filename) {
    #ifdef _WIN32
        return CreateFileA(
            filename,
            GENERIC_WRITE,
            0,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
    #else
        return open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    #endif
}

bool writeCaptureFrame(FileHandle handle, const void* data, size_t size) {
    #ifdef _WIN32
        DWORD bytesWritten;
        return WriteFile(handle, data, (DWORD)size, &bytesWritten, NULL) && bytesWritten == size;
    #else
        return (size_t)write(handle, data, size) == size;
    #endif
}

void closeCaptureFile(FileHandle handle) {
    #ifdef _WIN32
        CloseHandle(handle);
    #else
        close(handle);
    #endif
}

bool isValidHandle(FileHandle handle) {
    #ifdef _WIN32
        return handle != INVALID_HANDLE_VALUE;
    #else
        return handle != -1;
    #endif
}

bool saveJpeg(const char* filename, const int* image, int width, int height, int quality = 90) {
    if (!image || width <= 0 || height <= 0) {
        printf("ERROR: Invalid image parameters: image=%p, width=%d, height=%d\n", 
               image, width, height);
        return false;
    }

    tjhandle compressor = tjInitCompress();
    if (!compressor) {
        printf("ERROR: Failed to initialize JPEG compressor: %s\n", tjGetErrorStr());
        return false;
    }
    
    unsigned char* jpegBuf = NULL;
    unsigned long jpegSize = 0;
    
    // Calculate the proper stride (bytes per row)
    int stride = width * sizeof(int);
    
    // Try with BGRA format (common in Windows)
    int result = tjCompress2(compressor, 
                            (unsigned char*)image, 
                            width, stride, height, 
                            TJPF_BGRA,  // Try BGRA format first
                            &jpegBuf, &jpegSize, 
                            TJSAMP_420, quality, 
                            TJFLAG_FASTDCT);
    
    if (result != 0) {
        // If BGRA failed, try RGBA format
        result = tjCompress2(compressor, 
                           (unsigned char*)image, 
                           width, stride, height, 
                           TJPF_RGBA,
                           &jpegBuf, &jpegSize, 
                           TJSAMP_420, quality, 
                           TJFLAG_FASTDCT);
                           
        if (result != 0) {
            printf("ERROR: JPEG compression failed: %s\n", tjGetErrorStr());
            tjDestroy(compressor);
            return false;
        }
    }
    
    FILE* file = fopen(filename, "wb");
    if (!file) {
        printf("ERROR: Failed to open file %s for writing: %s\n", filename, strerror(errno));
        tjFree(jpegBuf);
        tjDestroy(compressor);
        return false;
    }
    
    size_t written = fwrite(jpegBuf, 1, jpegSize, file);
    if (written != jpegSize) {
        printf("ERROR: Failed to write all JPEG data to %s\n", filename);
        fclose(file);
        tjFree(jpegBuf);
        tjDestroy(compressor);
        return false;
    }
    
    fclose(file);
    tjFree(jpegBuf);
    tjDestroy(compressor);
    // printf("Wrote %s\n", filename); // Commented out to reduce spam
    return true;
}

// Global variables
bool g_bProgramRunning = true;
OverlayManager g_OverlayManager;
float g_fTargetYawOffset = 0.0f;  // Current yaw offset of target from center
float g_fTargetPitchOffset = 0.0f; // Current pitch offset of target from center
bool g_bTargetLocked = false;     // Whether the target position is locked
bool g_Recording = false; // is recording data
bool g_runningCalibration = false;
bool g_isTrained = false;
DashboardUI g_DashboardUI;
TrainerWrapper g_Trainer;
int g_currentFlags = 0;

// Global training progress display (set by subprocess thread, used by main thread)
std::string g_trainingProgressDisplay = "";
std::vector<float> g_trainingLossHistory;
bool g_hasTrainingUpdate = false;


std::unique_ptr<Ort::Session> g_OrtSession;
bool g_PreviewRunning = false;
std::string g_PreviewModelPath;
std::thread g_PreviewThread;
std::string g_outputModelPath;
bool g_StopPreviewThread = false;

// Function prototypes
void ProcessKeyboardInput();
void PrintInstructions();
void UpdateTargetPosition();
void Cleanup();

uint64_t current_time_ms(void) {
#ifdef _WIN32
    // Windows implementation for Unix timestamp (milliseconds)
    FILETIME ft;
    ULARGE_INTEGER uli;
    
    GetSystemTimeAsFileTime(&ft);
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    
    // Convert Windows FILETIME to Unix timestamp in milliseconds
    return (uint64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
#else
    // POSIX implementation
    struct timespec spec;
    
    if (clock_gettime(CLOCK_REALTIME, &spec) == 0) {
        return (uint64_t)(spec.tv_sec * 1000ULL + spec.tv_nsec / 1000000ULL);
    } else {
        // Fallback to gettimeofday if clock_gettime is not available
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return (uint64_t)(tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL);
    }
#endif
}

void print_active_flags(uint32_t flags) {
    printf("Active flags (0x%08X): ", flags);
    
    // Check routine flags
    for (int i = 0; i < 24; i++) {
        if (flags & (1U << i)) {
            printf("  FLAG_ROUTINE_%d", i + 1);
        }
    }
    
    // Check special flags
    if (flags & FLAG_CONVERGENCE) {
        printf("  FLAG_CONVERGENCE");
    }
    
    if (flags & FLAG_IN_MOVEMENT) {
        printf("  FLAG_IN_MOVEMENT");
    }
    
    if (flags & FLAG_RESTING) {
        printf("  FLAG_RESTING");
    }
    
    if (flags & FLAG_ROUTINE_COMPLETE) {
        printf("  FLAG_ROUTINE_COMPLETE");
    }
    
    // If no flags are set
    if (flags == 0) {
        printf("  No flags set");
    }
}

void RunPreviewInference(FrameBuffer* frameBufferLeft, FrameBuffer* frameBufferRight, OverlayManager* overlayManager) {
    // Print info about starting preview thread
    printf("Starting preview inference thread with model: %s\n", g_PreviewModelPath.c_str());
    
    try {
        // Set up ONNX Runtime session options
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
        
        // Create session
        printf("Loading ONNX model...\n");
        Ort::Env g_OrtEnv{ORT_LOGGING_LEVEL_WARNING, "BaballsPreview"};
        g_OrtSession = std::make_unique<Ort::Session>(g_OrtEnv, to_path_string(g_PreviewModelPath).c_str(), sessionOptions);
        printf("Model loaded successfully\n");
        
        // Get model info
        Ort::AllocatorWithDefaultOptions allocator;
        size_t numInputNodes = g_OrtSession->GetInputCount();
        size_t numOutputNodes = g_OrtSession->GetOutputCount();
        
        printf("Model has %zu input(s) and %zu output(s)\n", numInputNodes, numOutputNodes);
        
        // Get input name
        auto inputName = g_OrtSession->GetInputNameAllocated(0, allocator);
        printf("Input name: %s\n", inputName.get());
        
        // Get output name
        auto outputName = g_OrtSession->GetOutputNameAllocated(0, allocator);
        printf("Output name: %s\n", outputName.get());
        
        // Prepare input/output names
        const char* inputNames[] = {inputName.get()};
        const char* outputNames[] = {outputName.get()};
        
        // Main inference loop
        while (!g_StopPreviewThread) {
            // Get frames from both eyes
            /*int width, height;
            uint64_t time;
            const char* imageLeft = frameBufferLeft->getFrameCopy(&width, &height, &time);
            const char* imageRight = frameBufferRight->getFrameCopy(&width, &height, &time);
            
            if (width < 1 || height < 1 || !imageLeft || !imageRight) {
                // Invalid frame data, wait and try again
                if (imageLeft) free(imageLeft);
                if (imageRight) free(imageRight);
                std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps
                continue;
            }
            
            // Prepare input tensor data (combine left and right eye images)
            std::vector<float> inputData(2 * width * height);
            
            // Convert RGBA int data to float and normalize
            for (int i = 0; i < width * height; i++) {
                // Extract red channel and normalize to [0,1]
                inputData[i] = ((imageLeft[i] & 0xFF) / 255.0f);
                inputData[width * height + i] = ((imageRight[i] & 0xFF) / 255.0f);
            }
            
            // Define input tensor shape - [1, 2, height, width]
            std::vector<int64_t> inputShape = {1, 2, static_cast<int64_t>(height), static_cast<int64_t>(width)};
            
            // Create input tensor
            auto memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memoryInfo, inputData.data(), inputData.size(),
                inputShape.data(), inputShape.size()
            );
            
            // Run inference
            std::vector<Ort::Value> outputTensors;
            try {
                outputTensors = g_OrtSession->Run(
                    Ort::RunOptions{nullptr}, 
                    inputNames, &inputTensor, 1, 
                    outputNames, 1
                );
                
                // Get output data
                float* outputData = outputTensors[0].GetTensorMutableData<float>();
                size_t outputSize = outputTensors[0].GetTensorTypeAndShapeInfo().GetElementCount();
                
                if (outputSize >= 7) {  // Assuming model output has at least 7 values
                    // Extract pitch and yaw from model output
                    float predictedPitch = outputData[0];  // First value is pitch
                    float predictedYaw = outputData[1];    // Second value is yaw
                    
                    // De-normalize from [-1,1] to degrees (if your model outputs normalized values)
                    // Adjust these calculations based on your model's output range
                    float pitchDegrees = predictedPitch * 180.0f;  // Convert from [-1,1] to degrees
                    float yawDegrees = predictedYaw * 180.0f;      // Convert from [-1,1] to degrees
                    
                    // Update the overlay target position with predicted values
                    overlayManager->SetPreviewTargetPosition(yawDegrees, pitchDegrees);
                    
                    // Print predicted values occasionally (every ~1 second)
                    static int frameCount = 0;
                    if (frameCount % 30 == 0) {
                        printf("Predicted: Pitch=%.2f° Yaw=%.2f°\n", pitchDegrees, yawDegrees);
                    }
                    frameCount++;
                }
            }
            catch (const Ort::Exception& e) {
                printf("ONNX Runtime error during inference: %s\n", e.what());
            }
            
            // Free memory
            free(imageLeft);
            free(imageRight);
            
            // Sleep to maintain a reasonable frame rate
            std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30fps*/
        }
    }
    catch (const Ort::Exception& e) {
        printf("ONNX Runtime error: %s\n", e.what());
    }
    catch (const std::exception& e) {
        printf("Error in preview thread: %s\n", e.what());
    }
    
    printf("Preview inference thread stopped\n");
    g_PreviewRunning = false;
}

void initEyeConnections(FrameBuffer* frameBufferLeft, FrameBuffer* frameBufferRight){
    frameBufferLeft->start();
    frameBufferRight->start();

    // keep waiting until both streams have valid image data
    while(true){
        int width, height;
        uint64_t time;
        size_t size;
        unsigned char* image = frameBufferLeft->getFrameCopy(&width, &height, &time, &size);
        if(width < 1 || height < 1){
            printf("Waiting for valid image data from both eyes...\n");
            sleep(1000); /* TODO: universal sleep (this is windows.h) */
            free(image);
            continue;
        }
        free(image);
        
        image = frameBufferRight->getFrameCopy(&width, &height, &time, &size);
        if(width < 1 || height < 1){
            printf("Waiting for valid image data from both eyes...\n");
            sleep(1000); /* TODO: universal sleep (this is windows.h) */
            free(image);
            continue;
        }
        free(image);

        break;
    }
    printf("Eye streams started up!\n");
}


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
    #include <io.h>
    #include <fcntl.h>
    #define dup2 _dup2
    #define fileno _fileno
#else
    #include <unistd.h>
#endif

int redirectOutputToLogFile(const char* logFilePath) {
    char filename[100];
    FILE* logFile = NULL;
    
    // Store original stdout and stderr for diagnostic messages
    FILE* originalStdout = stdout;
    FILE* originalStderr = stderr;
    
    // Generate timestamp-based filename if none provided
    if (logFilePath == NULL) {
        time_t now = time(NULL);
        struct tm* timeinfo = localtime(&now);
        strftime(filename, sizeof(filename), "log_%Y%m%d_%H%M%S.txt", timeinfo);
        logFilePath = filename;
    }
    
    // First try opening the file directly to test permissions
    logFile = fopen(logFilePath, "w");
    if (logFile == NULL) {
        fprintf(originalStderr, "ERROR: Cannot open log file '%s': %s\n", 
                logFilePath, strerror(errno));
        return -1;
    }
    
    // Write a test message and flush it
    fprintf(logFile, "Log file initialized at: %s\n", __DATE__ " " __TIME__);
    fflush(logFile);
    fclose(logFile);
    
    // Now redirect stdout
    logFile = freopen(logFilePath, "a", stdout);
    if (logFile == NULL) {
        fprintf(originalStderr, "ERROR: Failed to redirect stdout: %s\n", strerror(errno));
        return -1;
    }
    
    // Redirect stderr separately (safer than using dup2)
    if (freopen(logFilePath, "a", stderr) == NULL) {
        fprintf(originalStdout, "ERROR: Failed to redirect stderr: %s\n", strerror(errno));
        return -1;
    }
    
    // Disable buffering to ensure output appears immediately
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    
    printf("Log started successfully at: %s\n", __DATE__ " " __TIME__);
    fflush(stdout);
    
    return 0;
}

int main(int argc, char* argv[])
{

    redirectOutputToLogFile("./calibration.log");

    // initialize rest server
    HTTPServer server(23951);

    FrameBuffer frameBufferLeft(128, 128, 30);
    FrameBuffer frameBufferRight(128, 128, 30);

    

    /*server.register_handler("/hello", [](const std::unordered_map<std::string, std::string>& params) {
        std::string name = params.count("name") ? params.at("name") : "World";
        return "Hello, " + name + "!";
    });
    
    server.register_handler("/echo", [](const std::unordered_map<std::string, std::string>& params) {
        std::stringstream ss;
        ss << "Parameters received: " << params.size() << "\n";
        for (const auto& param : params) {
            ss << param.first << ": " << param.second << "\n";
        }
        return ss.str();
    });*/


    // returns the status of the current calibration. if status=complete, you can use the checkpoint at the path specified in /start_calibration
    server.register_handler("/status", [](const std::unordered_map<std::string, std::string>& params){

        std::string sRunning = std::to_string(g_runningCalibration);
        std::string sRecording = std::to_string(g_Recording);

        std::string sIsCalibrationComplete = std::to_string(g_OverlayManager.g_routineController.isComplete() && g_isTrained);
        std::string sCurrentOpIndex = std::to_string(g_OverlayManager.g_routineController.getCurrentOperationIndex());
        std::string sMaxOpIndex = std::to_string(g_OverlayManager.g_routineController.getTotalOperationCount());
        std::string sIstrained = std::to_string(g_isTrained);

        return "{\"result\":\"ok\", \"running\":\""+sRunning+"\", \"recording\":\""+sRecording+"\", \"calibrationComplete\":\""+sIsCalibrationComplete+"\", \"isTrained\":\""+sIstrained+"\", \"currentIndex\":"+sCurrentOpIndex+", \"maxIndex\":"+sMaxOpIndex+"}";
    });

    server.register_handler("/settings", [](const std::unordered_map<std::string, std::string>& params){
        return "{\"result\":\"ok\"}";
    });

    server.register_handler("/set_target", [](const std::unordered_map<std::string, std::string>& params){
        g_PreviewRunning = true;

        if(params.count("pitch") < 1 || params.count("yaw") < 1)
            return "{\"result\":\"fail: please specify pitch and yaw values\"}";

        float pitch = std::stof(params.at("pitch"));
        float yaw = std::stof(params.at("yaw"));

        g_OverlayManager.SetPreviewTargetPosition(yaw, pitch);

        return "{\"result\":\"ok\"}";
    });

    server.register_handler("/start_cameras", [&frameBufferLeft, &frameBufferRight](const std::unordered_map<std::string, std::string>& params){
        printf("Got start_cameras\n");
        int width, height;
        printf("Param counts: %zi %zi\n", params.count("left"), params.count("right"));
        std::string sWidth = std::to_string(0);
        std::string sHeight = std::to_string(0);
        try{
            printf("%s\n%s\n", params.at("left").c_str(), params.at("right").c_str());
            frameBufferRight.setURL(params.at("left").c_str());
            frameBufferLeft.setURL(params.at("right").c_str());

            printf("Init eye connection...\n");
            
            initEyeConnections(&frameBufferLeft, &frameBufferRight);
            
            printf("Get frame copy...\n");
            uint64_t time;
            size_t size;
            unsigned char* image = frameBufferLeft.getFrameCopy(&width, &height, &time, &size);
            free(image);
            
            printf("Got!\n");
            sWidth = std::to_string(width);
            sHeight = std::to_string(height);
        }catch (const std::exception& e) {
            printf("Error in preview thread: %s\n", e.what());
        }
        return "{\"result\":\"ok\", \"width\": " + sWidth + ", \"height\": " + sHeight + "}";
    });

    server.register_handler("/start_calibration", [](const std::unordered_map<std::string, std::string>& params){
        if (params.count("routine_id") == 0 || params.count("onnx_filename") == 0) {
            return "{\"result\":\"error\", \"message\":\"please specify a routine_id and onnx_filename\"}";
        }

        std::string decodedPath = params.at("onnx_filename");
        size_t pos = 0;
        while ((pos = decodedPath.find('%', pos)) != std::string::npos) {
            if (pos + 2 < decodedPath.length()) {
                int hexValue;
                std::istringstream iss(decodedPath.substr(pos + 1, 2));
                iss >> std::hex >> hexValue;
                decodedPath.replace(pos, 3, 1, static_cast<char>(hexValue));
            } else {
                break;
            }
        }
        
        g_outputModelPath = decodedPath;    

        g_OverlayManager.StartRoutine((uint32_t) std::stoi(params.at("routine_id")));

        g_runningCalibration = true;
        g_Recording = true;
        return "{\"result\":\"ok\"}";
    });

    server.register_handler("/start_preview", [&frameBufferLeft, &frameBufferRight](const std::unordered_map<std::string, std::string>& params) {
        // Stop any existing preview
        if (g_PreviewRunning) {
            g_StopPreviewThread = true;
            if (g_PreviewThread.joinable()) {
                g_PreviewThread.join();
            }
            g_PreviewRunning = false;
            g_StopPreviewThread = false;
        }
        
        // Check for model path parameter
        if (params.count("model_path") == 0) {
            return "{\"result\":\"error\", \"message\":\"model_path parameter is required\"}";
        }
        
        g_PreviewModelPath = params.at("model_path");
        
        // Validate that the model file exists
        std::ifstream modelFile(g_PreviewModelPath);
        if (!modelFile.good()) {

            return "{\"result\":\"error\"}";
        }
        modelFile.close();
        
        // Start the preview thread
        g_PreviewRunning = true;
        g_StopPreviewThread = false;
        g_PreviewThread = std::thread(RunPreviewInference, &frameBufferLeft, &frameBufferRight, &g_OverlayManager);

        return "{\"result\":\"ok\"}";
    });

    server.register_handler("/stop_preview", [](const std::unordered_map<std::string, std::string>& params) {
        if (g_PreviewRunning) {
            g_StopPreviewThread = true;
            if (g_PreviewThread.joinable()) {
                g_PreviewThread.join();
            }
            g_PreviewRunning = false;
            g_StopPreviewThread = false;
            return "{\"result\":\"ok\", \"message\":\"Preview stopped\"}";
        } else {
            return "{\"result\":\"ok\", \"message\":\"No preview was running\"}";
        }
    });

    server.register_post_handler("/start_calibration_json", [](const auto& params, const std::string& body) {
        // Process POST request with body

        // parse the json

        // start the cameras

        // start the calibration


        return "{\"result\":\"ok\", \"message\":\"Started calibration!\"}";
        //return "{\"message\": \"POST request processed\", \"body_size\": " + 
        //      std::to_string(body.length()) + ", \"body\": \"" + body + "\"}";
    });
    

    server.start();

    //Sleep(1000000);

    //test
    /*CaptureFrame frame2;
    char filename2[256];
    uint64_t start_time2 = current_time_ms();
    snprintf(filename2, sizeof(filename2), "capture_test_%llu.bin", start_time2);
    FileHandle captureFile2 = openCaptureFile(filename2);
    if (!isValidHandle(captureFile2)) {
        printf("ERROR: Failed to open capture file!\n");
        return -1;
    }
    for(int i = 0; i < 10; i++) {
        if(true) {
            int width, height;
            int* imageLeft = frameBufferLeft.getFrameCopy(&width, &height);
            int* imageRight = frameBufferRight.getFrameCopy(&width, &height);
            uint64_t now = current_time_ms();


            memcpy(frame2.image_data_left, imageLeft, width*height*sizeof(int));
            memcpy(frame2.image_data_right, imageRight, width*height*sizeof(int));
    
            frame2.timestampLow = (uint32_t)(now & 0xFFFFFFFF);
            frame2.timestampHigh = (uint32_t)((now >> 32) & 0xFFFFFFFF);
    
            if (!writeCaptureFrame(captureFile2, &frame2, sizeof(frame2))) {
                printf("ERROR: Failed to write frame! \n");
            }
            
            // Save JPEG files
            char filenameLeft[100], filenameRight[100];
            sprintf(filenameLeft, "frame_%03d_left.jpg", i);
            sprintf(filenameRight, "frame_%03d_right.jpg", i);

            printf("Wrote ");
            printf(filenameRight);
            printf("\n");
            
            saveJpeg(filenameLeft, imageLeft, width, height);
            saveJpeg(filenameRight, imageRight, width, height);
            
            free(imageLeft); 
            free(imageRight);
        }
        Sleep(1000 / 30);
    }
    closeCaptureFile(captureFile2);*/

    // Initialize OpenVR
    vr::EVRInitError eError = vr::VRInitError_None;
    vr::IVRSystem* pVRSystem = vr::VR_Init(&eError, vr::VRApplication_Overlay);
    
    if (eError != vr::VRInitError_None)
    {
        printf("ERROR: Failed to initialize OpenVR: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
        return -1;
    }
    
    printf("OpenVR initialized successfully\n");
    
    
    // Create the overlay manager
    OverlayManager overlayManager;
    
    // Initialize the overlay
    if (!overlayManager.Initialize())
    {
        printf("ERROR: Failed to initialize overlay\n");
        vr::VR_Shutdown();
        return -1;
    }


    printf("Overlay initialized successfully\n");

    if (!g_DashboardUI.Initialize()) {
        printf("ERROR: Failed to initialize dashboard UI\n");
        // Continue anyway, as this is not critical
    }else{
        g_DashboardUI.AddButton("Start", 20, 20, 200, 60, [&overlayManager]() { 
            // Start measurement
            printf("Starting measurement...\n");
            g_Recording = true;
            // Your start logic here
        });
        
        g_DashboardUI.AddButton("Reset", 20, 100, 200, 60, [&overlayManager]() { 
            // Reset target position
            printf("Resetting target position...\n");
            g_Recording = false;
            overlayManager.ResetTargetPosition();
        });
        
        g_DashboardUI.AddButton("Set Position", 20, 180, 200, 60, [&overlayManager]() { 
            // Open dialog for setting position
            printf("Enter new target position (yaw pitch): ");
            float newYaw, newPitch;
            scanf("%f %f", &newYaw, &newPitch);
            overlayManager.SetFixedTargetPosition(newYaw, newPitch);
            printf("Target position set to: Yaw %.1f°, Pitch %.1f°\n", newYaw, newPitch);
        });
    }
    
    // Variables to track target lock state
    bool isTargetLocked = false;
    
    char filename[256];
    uint64_t start_time = current_time_ms();
    snprintf(filename, sizeof(filename), "capture_%" PRIu64".bin", start_time);
    
    FileHandle captureFile = openCaptureFile(filename);
    if (!isValidHandle(captureFile)) {
        printf("ERROR: Failed to open capture file!\n");
        return -1;
    }

    // Main application loop
    CaptureFrame frame;
    char str[1024];
    int remTime = 0;
    bool bQuit = false;
    bool goodData = false;
    while (!bQuit)
    {
        // Process SteamVR events
        goodData = false;
        vr::VREvent_t event;
        while (vr::VRSystem()->PollNextEvent(&event, sizeof(event)))
        {
            // Handle SteamVR events
            switch (event.eventType)
            {
                case vr::VREvent_Quit:
                    printf("SteamVR is shutting down\n");
                    bQuit = true;
                    break;
            }
        }
        
        if (g_runningCalibration || g_PreviewRunning || g_Trainer.isRunning())
        {
            overlayManager.Update();
        }
        
        g_DashboardUI.Update();
        
        g_OverlayManager.UpdateAnimation();

        // Reset blendshape values for each frame
        frame.routineLeftLid = 0.0f;
        frame.routineRightLid = 0.0f;
        frame.routineBrowRaise = 0.0f;
        frame.routineBrowAngry = 0.0f;
        frame.routineWiden = 0.0f;
        frame.routineSquint = 0.0f;
        frame.routineDilate = 0.0f;

        //printf("DEBUG: Current routine stage = %d\n", RoutineController::m_routineStage);
        switch(RoutineController::m_routineStage){
            case 0: // pre-calibration stage
            default:
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Gaze Calibration ~~ \n\nThere are multiple stages to this calibration routine.\nIn this first sage, the crosshair will move in an S pattern.\nPlease follow the crosshair until the routine completes.\n\nThis will start in %d seconds.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.HideTargetCrosshair();
                break;
            case 1: // scan left-right
            case 2: // scan up-down
                goodData = true;
                overlayManager.SetDisplayString(NULL); // HACK! Todo: only call once!
                overlayManager.ShowTargetCrosshair();
                break;
            case 3: // notify of closed eyes
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Eyelid Calibration ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, close both your eyes for 5 seconds.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.HideTargetCrosshair();
                break;
            case 4: // closed eyes
                goodData = true;
                overlayManager.SetDisplayString("   ~~ Eyelid Calibration ~~ \n\nKeep your eyes closed!");
                overlayManager.HideTargetCrosshair();
                frame.routineLeftLid = 1.0f;  // Fully closed
                frame.routineRightLid = 1.0f; // Fully closed
                break;
            case 5: // notify of half closed eyes
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Eyelid Calibration ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, do bedroom eyes for 5 seconds (eyes half closed).\nLook straight forward at the crosshair.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.ShowTargetCrosshair();
                break;
            case 6: // half closed eyes
                goodData = true;
                overlayManager.SetDisplayString("   ~~ Eyelid Calibration ~~ \n\nKeep your eyes half closed!\nLook straight forward at the crosshair.");
                overlayManager.ShowTargetCrosshair();
                frame.routineLeftLid = 0.5f;  // Half closed
                frame.routineRightLid = 0.5f; // Half closed
                //frame.routineSquint = 0.5f;   // Squinting
                break;
            case 7: // notify of wink left
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Eyelid Calibration ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, close your left eye for 5 seconds.\nLook straight forward at the crosshair.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.ShowTargetCrosshair();
                break;
            case 8: // wink left
                goodData = true;
                overlayManager.SetDisplayString("   ~~ Eyelid Calibration ~~ \n\nKeep your left eye closed!\nLook straight forward at the crosshair.");
                overlayManager.ShowTargetCrosshair();
                frame.routineLeftLid = 1.0f;  // Left eye closed
                frame.routineRightLid = 0.0f; // Right eye open
                break;
            case 9: // notify of wink right
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Eyelid Calibration ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, close your right eye for 5 seconds.\nLook straight forward at the crosshair.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.ShowTargetCrosshair();
                break;
            case 10: // wink right
                goodData = true;
                overlayManager.SetDisplayString("   ~~ Eyelid Calibration ~~ \n\nKeep your right eye closed!\nLook straight forward at the crosshair.");
                overlayManager.ShowTargetCrosshair();
                frame.routineLeftLid = 0.0f;  // Left eye open
                frame.routineRightLid = 1.0f; // Right eye closed
                break;
            case 11: // notify of eye widen
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Eyelid Calibration ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, widen your eyes for 5 seconds.\n(Surprise face!)\nLook straight forward at the crosshair.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.ShowTargetCrosshair();
                break;
            case 12: // eye widen
                goodData = true;
                overlayManager.SetDisplayString("   ~~ Eyelid Calibration ~~ \n\nKeep your eyes wide open!\nSurprise face! Look straight forward at the crosshair.");
                overlayManager.ShowTargetCrosshair();
                frame.routineWiden = 1.0f; // Raise eyebrows for surprise
                break;
            case 13: // notify of eye angry
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Eyelid Calibration ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, lower your brow for 5 seconds.\n(Angry eyes!)\nLook straight forward at the crosshair.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.ShowTargetCrosshair();
                break;
            case 14: // eye angry
                goodData = true;
                overlayManager.SetDisplayString("   ~~ Eyelid Calibration ~~ \n\nKeep your eyebrows lowered!\nAngry expression! Look straight forward at the crosshair.");
                overlayManager.ShowTargetCrosshair();
                frame.routineBrowAngry = 1.0f; // Lower eyebrows for angry expression
                break;
            case 15: // notify of convergence test
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Eye Convergence Test ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, follow the crosshair as it moves towards and away from you.\nKeep your eyes focused on the crosshair.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.HideTargetCrosshair();
                break;
            case 16: // convergence test
                goodData = true;
                overlayManager.SetDisplayString(NULL);
                overlayManager.ShowTargetCrosshair();
                break;
            case 17: // notify of dilation calibration
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Pupil Dilation Calibration ~~ \n\nCountdown: %d seconds!\n\nWhen the countdown finishes, look straight ahead.\nThe screen will show different brightness levels to calibrate pupil dilation.\nThis process should take about a minute.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.HideTargetCrosshair();
                break;
            case 18: // black screen (fully dilated)
                goodData = true;
                overlayManager.SetDisplayString(NULL);
                overlayManager.ShowTargetCrosshair();
                frame.routineDilate = 1.0f; // Fully dilated
                break;
            case 19: // notify of white screen
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Pupil Dilation Calibration ~~ \n\nCountdown: %d seconds!\n\nNext: bright white screen.\nLook straight ahead and let your pupils adjust.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.ShowTargetCrosshair();
                break;
            case 20: // white screen (fully constricted)
                goodData = true;
                overlayManager.SetDisplayString(NULL);
                overlayManager.ShowTargetCrosshair();
                frame.routineDilate = 0.0f; // Fully constricted
                break;
            case 21: // notify of gradient fade
                remTime = g_OverlayManager.g_routineController.getTimeTillNext();
                sprintf(str, "   ~~ Pupil Dilation Calibration ~~ \n\nCountdown: %d seconds!\n\nNext: screen will gradually fade from white to black.\nKeep looking straight ahead.", remTime);
                overlayManager.SetDisplayString(str);
                overlayManager.ShowTargetCrosshair();
                break;
            case 22: // gradient fade (white to black)
                goodData = true;
                overlayManager.SetDisplayString(NULL);
                overlayManager.ShowTargetCrosshair();
                // routineDilate is set dynamically based on screen brightness
                // 0.0 = bright screen (pupils constricted), 1.0 = dark screen (pupils dilated)
                frame.routineDilate = g_OverlayManager.s_routineFadeProgress; // Uses fade progress from routine controller
                break;
            case 23: // completion stage
                printf("DEBUG: In case 23 - g_Trainer.isRunning()=%d, g_hasTrainingUpdate=%d\n", g_Trainer.isRunning(), g_hasTrainingUpdate);
                if (g_Trainer.isRunning()) {
                    // Training is active - use safe combined text and graph display
                    if (g_hasTrainingUpdate) {
                        printf("DEBUG: Updating training display with text length=%zu, loss history size=%zu\n", 
                               g_trainingProgressDisplay.length(), g_trainingLossHistory.size());
                        // Use the new thread-safe method that combines text and graph rendering
                        overlayManager.SetDisplayStringWithGraph(g_trainingProgressDisplay.c_str(), g_trainingLossHistory);
                        g_hasTrainingUpdate = false; // Reset flag
                    } else {
                        // No new update - keep showing the last progress display with graph
                        printf("DEBUG: No training update, keeping last display with graph\n");
                        // Reuse the last training progress display to avoid flashing
                        if (!g_trainingProgressDisplay.empty()) {
                            overlayManager.SetDisplayStringWithGraph(g_trainingProgressDisplay.c_str(), g_trainingLossHistory);
                        } else {
                            // Only show placeholder if we don't have any progress yet
                            overlayManager.SetDisplayStringWithGraph("   ~~ Neural Network Training ~~ \n\nTraining in progress...\nPlease wait.", g_trainingLossHistory);
                        }
                    }
                    overlayManager.ShowTargetCrosshair();
                } else {
                    printf("DEBUG: Training not running, showing completion message\n");
                    // Training not running - show completion message
                    overlayManager.SetDisplayString("   ~~ Calibration Complete ~~ \n\nCalibration routine has finished successfully.\nThank you for your patience!");
                    overlayManager.ShowTargetCrosshair();
                }
                break;
        }


        //printf(str);
        

        OverlayManager::ViewingAngles angles = overlayManager.CalculateCurrentViewingAngle();
        if(g_Recording){
            if(OverlayManager::s_routineState == FLAG_ROUTINE_COMPLETE){
                g_Recording = false;
                closeCaptureFile(captureFile);

                printf("Starting trainer with capture file: %s\n", filename);

                g_Trainer.start(filename, g_outputModelPath,
                    [](const std::string& output){
                        printf("trainer output: %s", output.c_str());
                    },
                    [](const TrainerProgress& progress){
                        printf("DEBUG: Trainer progress callback invoked - isTraining=%d, isComplete=%d, hasError=%d\n", 
                               progress.isTraining, progress.isComplete, progress.hasError);
                        // Set global training progress (to be used by main thread)
                        std::string progressDisplay = "   ~~ Neural Network Training ~~ \n\n";
                        
                        if (progress.isComplete) {
                            progressDisplay += "Training Complete!\n";
                            progressDisplay += "Final Loss: " + std::to_string(progress.epochAverageLoss);
                        } else if (progress.hasError) {
                            progressDisplay += "Training Error:\n" + progress.lastError;
                        } else if (progress.isTraining) {
                            // Epoch progress
                            if (progress.totalEpochs > 0) {
                                float epochProgress = static_cast<float>(progress.currentEpoch) / progress.totalEpochs;
                                progressDisplay += "Epoch: " + std::to_string(progress.currentEpoch) + "/" + std::to_string(progress.totalEpochs) + "\n";
                                
                                // ASCII progress bar
                                int barWidth = 20;
                                int pos = static_cast<int>(barWidth * epochProgress);
                                std::string bar = "[";
                                for (int i = 0; i < barWidth; ++i) {
                                    if (i < pos) bar += "|";
                                    //else if (i == pos) bar += "▌";
                                    else bar += ".";
                                }
                                bar += "] " + std::to_string(static_cast<int>(epochProgress * 100)) + "%\n\n";
                                progressDisplay += bar;
                            }
                            
                            // Batch progress
                            if (progress.totalBatches > 0) {
                                float batchProgress = static_cast<float>(progress.currentBatch) / progress.totalBatches;
                                progressDisplay += "Batch: " + std::to_string(progress.currentBatch) + "/" + std::to_string(progress.totalBatches) + "\n";
                                
                                int barWidth = 20;
                                int pos = static_cast<int>(barWidth * batchProgress);
                                std::string bar = "[";
                                for (int i = 0; i < barWidth; ++i) {
                                    if (i < pos) bar += "|";
                                    //else if (i == pos) bar += "▌";
                                    else bar += ".";
                                }
                                bar += "] " + std::to_string(static_cast<int>(batchProgress * 100)) + "%\n\n";
                                progressDisplay += bar;
                            }
                            
                            // Current loss
                            if (progress.currentLoss > 0) {
                                progressDisplay += "Current Loss: " + std::to_string(progress.currentLoss) + "\n";
                            }
                            if (progress.epochAverageLoss > 0) {
                                progressDisplay += "Epoch Avg: " + std::to_string(progress.epochAverageLoss) + "\n";
                            }
                            
                            // ETA calculation
                            auto now = std::chrono::steady_clock::now();
                            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - progress.startTime).count();
                            if (progress.totalEpochs > 0 && progress.currentEpoch > 0 && elapsed > 0) {
                                float epochsPerSecond = static_cast<float>(progress.currentEpoch) / elapsed;
                                int remainingEpochs = progress.totalEpochs - progress.currentEpoch;
                                float etaSeconds = remainingEpochs / epochsPerSecond;
                                
                                int eta_hours = static_cast<int>(etaSeconds) / 3600;
                                int eta_minutes = (static_cast<int>(etaSeconds) % 3600) / 60;
                                int eta_secs = static_cast<int>(etaSeconds) % 60;
                                
                                progressDisplay += "ETA: ";
                                if (eta_hours > 0) progressDisplay += std::to_string(eta_hours) + "h ";
                                if (eta_minutes > 0 || eta_hours > 0) progressDisplay += std::to_string(eta_minutes) + "m ";
                                progressDisplay += std::to_string(eta_secs) + "s\n";
                            }
                            
                            // Add graph label if we have data
                            if (progress.lossHistory.size() > 1) {
                                progressDisplay += "\nLoss Trend Graph:\n";
                            }
                        } else {
                            progressDisplay += "Training is getting started, please wait...";
                        }
                        
                        // Set global variables for main thread to use
                        g_trainingProgressDisplay = progressDisplay;
                        g_trainingLossHistory = progress.lossHistory;
                        g_hasTrainingUpdate = true;
                        printf("DEBUG: Set g_hasTrainingUpdate=true, progressDisplay length=%zu, lossHistory size=%zu\n", 
                               progressDisplay.length(), progress.lossHistory.size());
                    },
                    [](){
                        printf("trainer finished!");
                        g_isTrained = true;
                    }
                );
            }else{
                if(true){//if(OverlayManager::s_routineState == FLAG_RESTING && !RoutineController::m_stepWritten){
                    //OverlayManager::s_routineState = FLAG_IN_MOVEMENT;
                    //RoutineController::m_stepWritten = true;
                    int width, height;
                    uint64_t time_left, time_right;
                    size_t size_left, size_right;
                    unsigned char* imageLeft = frameBufferLeft.getFrameCopy(&width, &height, &time_left, &size_left);
                    unsigned char* imageRight = frameBufferRight.getFrameCopy(&width, &height, &time_right, &size_right);

                    /*FILE* fp = fopen("./good_data4.bin", "wb");
                    printf("Writing good data...\n");
                    if (fp) {
                        fwrite(imageLeft, 1, size_left, fp);
                        fclose(fp);
                        printf("Dumped bad JPEG data to bad_data4.bin (%u bytes)\n", size_left);
                    }*/
                    uint64_t now = current_time_ms();

                    //memcpy(frame.image_data_left, imageLeft, width*height*sizeof(int));
                    //memcpy(frame.image_data_right, imageRight, width*height*sizeof(int));

                    frame.routinePitch = OverlayManager::s_routinePitch;//(int32_t)(OverlayManager::s_routinePitch * FLOAT_TO_INT_CONSTANT);
                    frame.routineYaw = OverlayManager::s_routineYaw;//(int32_t)(OverlayManager::s_routineYaw * FLOAT_TO_INT_CONSTANT);
                    frame.routineDistance = OverlayManager::s_routineDistance;//(int32_t)(OverlayManager::s_routineDistance * FLOAT_TO_INT_CONSTANT);

                    frame.jpeg_data_left_length = (uint32_t)size_left;
                    frame.jpeg_data_right_length = (uint32_t)size_right;

                    if(!RoutineController::m_stepWritten){
                        RoutineController::m_stepWritten = true;
                        frame.routineState = FLAG_RESTING;
                    }else{
                        frame.routineState = FLAG_IN_MOVEMENT;
                    }

                    if(goodData)
                        frame.routineState |= FLAG_GOOD_DATA;
                    //frame.routineState = OverlayManager::s_routineState;//(uint32_t)OverlayManager::s_routineState;
                    // printf("Time_left: %lld, time_right: %lld, now: %lld\n", time_left, time_right, now); // Commented out to reduce spam
                    // printf("Routine position: %f %f, time diffL: %lld, time diffR: %lld ", frame.routinePitch, frame.routineYaw, now - time_left, now - time_right); // Commented out to reduce spam
                    // print_active_flags(OverlayManager::s_routineState); // Commented out to reduce spam
                    // printf("\n"); // Commented out to reduce spam

                    //make this a int64?//
                    //frame.timestampLow = (uint32_t)(now & 0xFFFFFFFF);
                    ///frame.timestampHigh = (uint32_t)((now >> 32) & 0xFFFFFFFF);

                    //frame.videoTimestampLow = (uint32_t)(time & 0xFFFFFFFF);
                    //frame.videoTimestampHigh = (uint32_t)((time >> 32) & 0xFFFFFFFF);

                    frame.timestamp = now;
                    frame.timestamp_left = time_left;
                    frame.timestamp_right = time_right;

                    // printf("frame size: %lld", sizeof(frame)); // Commented out to reduce spam

                    if (!writeCaptureFrame(captureFile, &frame, sizeof(frame))) {
                        printf("ERROR: Failed to write frame! (metadata)\n");
                    }

                    if (!writeCaptureFrame(captureFile, imageLeft, size_left)) {
                        printf("ERROR: Failed to write frame! (left eye)\n");
                    }

                    if (!writeCaptureFrame(captureFile, imageRight, size_right)) {
                        printf("ERROR: Failed to write frame! (right eye)\n");
                    }
                    
                    free(imageLeft); 
                    free(imageRight);
                }
            }
        }

        if(g_Recording && false){
            /*int width, height;
            uint64_t time;
            int* frame = frameBufferLeft.getFrameCopy(&width, &height, &time);

            uint64_t now = current_time_ms();

            // convert tracking data to int, for simpler storage
            int label[] = {
                width,
                height,
                (int)(OverlayManager::s_routinePitch * FLOAT_TO_INT_CONSTANT),
                (int)(OverlayManager::s_routineYaw * FLOAT_TO_INT_CONSTANT),
                (int)(OverlayManager::s_routineDistance * FLOAT_TO_INT_CONSTANT), // target distance (for convergence)
                0, // fov adjustment distance
                (int)(now & 0xFFFFFFFF),          // Low 32 bits of timestamp
                (int)((now >> 32) & 0xFFFFFFFF),  // High 32 bits of timestamp
                (int)OverlayManager::s_routineState // flags (see flags.h)
            };

            size_t frame_size = (size_t)(width * height * sizeof(int));

            int* frame_and_meta = (int*) malloc(sizeof(label) + frame_size);

            memcpy(frame_and_meta, label, sizeof(label));
            memcpy((uint8_t*)(frame_and_meta) + sizeof(label), frame, frame_size);

            free(frame);

            printf("Appending %u %u %u %f %f %u\n", width, height, 
                (unsigned int)(sizeof(label) + frame_size), 
                OverlayManager::s_routinePitch, 
                OverlayManager::s_routineYaw,
                OverlayManager::s_routineState);

            NumPyIO::AppendToNumpyArray("./calibration.np", (const void*)frame_and_meta, sizeof(label) + frame_size, NumPyDataType::INT32);

            free(frame_and_meta);*/
        }
        /*printf("Peripheral vision measurement:\n");
        printf("  Yaw (horizontal): %.1f degrees\n", angles.yaw);
        printf("  Pitch (vertical): %.1f degrees\n", angles.pitch);
        printf("  Roll (tilt): %.1f degrees\n", angles.roll);
        printf("  Total angle: %.1f degrees\n", angles.total);*/

        char statusText[256];
        sprintf(statusText, "Yaw: %.1f° Pitch: %.1f° Total: %.1f°", 
                angles.yaw, angles.pitch, angles.total);
        
        g_DashboardUI.SetStatusText(statusText);

        

        sleep(10);
    }

    closeCaptureFile(captureFile);
    
    // Cleanup
    overlayManager.Shutdown();
    vr::VR_Shutdown();

    if (g_PreviewRunning) {
        g_StopPreviewThread = true;
        if (g_PreviewThread.joinable()) {
            g_PreviewThread.join();
        }
    }
    
    printf("Application closed\n");
    return 0;
}

void ProcessKeyboardInput()
{
   
}

void UpdateTargetPosition()
{
    // This function can be extended to implement automatic target movement
    // For now, we just use the keyboard input to adjust the position
}

void Cleanup()
{
    // Shutdown the overlay manager
    g_OverlayManager.Shutdown();
    
    // Shutdown SteamVR
    vr::VR_Shutdown();

    g_DashboardUI.Shutdown();
    
    std::cout << "\nApplication terminated." << std::endl;
}