#include "routine.h"
#include "config.h"
#include "routines.h" // Your header with the calibration routines
#include "flags.h"
#include "overlay_manager.h"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <regex>
#include <iostream>
#include <random>


#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
void beep(int frequency, int duration) {
    Beep(frequency, duration);
}
#elif __APPLE__
#include <AudioToolbox/AudioToolbox.h>
void beep(int frequency, int duration) {
    AudioServicesPlaySystemSound(kSystemSoundID_UserPreferredAlert);
}
#elif __linux__
#include <stdlib.h>
void beep(int frequency, int duration) {
    char command[50];
    snprintf(command, sizeof(command), "beep -f %d -l %d", frequency, duration);
    system(command);
}
#else
void beep(int frequency, int duration) {
    printf("\007"); // Fallback ASCII BEL character
    fflush(stdout);
}
#endif

// Constants for angle conversion
constexpr float MAX_YAW_ANGLE = 45.0f;    // Maximum yaw angle in degrees
constexpr float MAX_PITCH_ANGLE = 30.0f;  // Maximum pitch angle in degrees

#define TIME_BETWEEN_ROUTINES 30
#define STAGE_NOTIFICATION_DURATION 20.0f  // 20 seconds for countdown stages
#define STAGE_ACTION_DURATION 5.0f        // 5 seconds for action stages  
#define CONVERGENCE_TEST_DURATION 20.0f   // 20 seconds for convergence test
#define DILATION_ACTION_DURATION 10.0f    // 10 seconds for black/white screens
#define DILATION_GRADIENT_DURATION 30.0f  // 30 seconds for white-to-black fade

bool RoutineController::m_stepWritten = false;
double_t RoutineController::m_globalAdvancedTime = 0.0;
int RoutineController::m_routineStage = 0;
double_t RoutineController::m_stageStartTime = 0.0;

RoutineController::RoutineController(float maxMoveSpeed)
    : m_currentOpIndex(0)
    , m_routineStarted(false)
    , m_maxMoveSpeed(maxMoveSpeed)
    , m_elapsedTime(0.0)
    , m_lastRandomPointTime(0.0)
{
    // Initialize positions
    TargetPosition current;
    TargetPosition target;

    current.pitch = target.pitch = 0;
    current.yaw = target.yaw = 0;
    current.state = target.state = 0;

    m_currentPosition = current;
    m_targetPosition = target;
}

float getRandomFloat() {
    return -1.0f + 2.0f * static_cast<float>(static_cast<double>(rand()) / RAND_MAX);
}

/**
 * Gets the current state flags for this routine
 * 
 * @return An integer with flags representing the current routine state
 */
uint32_t RoutineController::getStateFlags() const 
{
    // Hard-coded approach to ensure correct flag generation
    uint32_t flags = 0;
    
    if (m_loadedRoutineIndex >= 0 && m_loadedRoutineIndex < 24) {
        flags |= (1U << (m_loadedRoutineIndex - 1));
    }
    
    // Determine current state directly
    if (!m_operations.empty() && m_currentOpIndex < m_operations.size()) {
        const Operation& op = m_operations[m_currentOpIndex];
        
        if (op.type == OperationType::REST) {
            flags = flags | FLAG_RESTING; // Explicit OR operation
        }else if (op.type == OperationType::SMOOTH || op.type == OperationType::SMOOTH_CIRCLE) {
            flags = flags | FLAG_IN_MOVEMENT; // Explicit OR operation
        }else if(op.type == OperationType::MOVE_AWAY_TOWARD){
            flags = flags | FLAG_CONVERGENCE;
        }
    }
    
    return flags;
}

int RoutineController::getTimeTillNext(){
    // For stages 0-2, use the original logic
    if(m_routineStage <= 2) {
        return TIME_BETWEEN_ROUTINES - (int)(m_globalAdvancedTime);
    }
    
    // For stages 3+, calculate time remaining in current stage
    double_t stageElapsed = m_globalAdvancedTime - m_stageStartTime;
    float stageDuration = STAGE_NOTIFICATION_DURATION; // Default for notification stages
    
    // Action stages have different duration
    if(m_routineStage % 2 == 0 && m_routineStage >= 4) {
        if(m_routineStage == CONVERGENCE_STAGE) {
            stageDuration = CONVERGENCE_TEST_DURATION;
        } else if(m_routineStage == DILATION_BLACK_STAGE || m_routineStage == DILATION_WHITE_STAGE) {
            stageDuration = DILATION_ACTION_DURATION;
        } else if(m_routineStage == DILATION_GRADIENT_STAGE) {
            stageDuration = DILATION_GRADIENT_DURATION;
        } else {
            stageDuration = STAGE_ACTION_DURATION;
        }
    }
    
    int timeLeft = (int)(stageDuration - stageElapsed);
    return timeLeft > 0 ? timeLeft : 0;
}

bool RoutineController::parseRoutine(const std::string& routineStr)
{
    // Clear any existing operations
    m_operations.clear();
    m_currentOpIndex = 0;
    m_routineStarted = false;
    
    // Split the routine string by semicolons
    std::istringstream stream(routineStr);
    std::string opStr;
    
    while (std::getline(stream, opStr, ';')) {
        if (!opStr.empty()) {
            if (!parseOperation(opStr)) {
                std::cerr << "Failed to parse operation: " << opStr << std::endl;
                m_operations.clear();
                return false;
            }
        }
    }
    
    return !m_operations.empty();
}

bool RoutineController::loadRoutine(int routineIndex) {

    printf("Loading routine %i\n", routineIndex);
    // Array of routine strings
    const char* routines[] = ALL_ROUTINES;
    
    // Check if index is valid
    if (routineIndex < 0 || routineIndex >= NUM_CALIBRATION_ROUTINES) {
        std::cerr << "Invalid routine index: " << routineIndex << std::endl;
        return false;
    }
    
    // Store the loaded routine index
    m_loadedRoutineIndex = routineIndex;
    
    // Parse the selected routine
    return parseRoutine(routines[routineIndex]);
}

bool RoutineController::parseOperation(const std::string& opStr)
{
    Operation op;
    op.elapsedTime = 0.0f;
    
    // Regular expressions for different operation types
    static const std::regex moveRegex(R"(move\s*\(\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*\))");
    static const std::regex restRegex(R"(rest\s*\(\s*([0-9]+\.[0-9]+)\s*\))");
    static const std::regex smoothRegex(R"(smooth\s*\(\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*\))");
    static const std::regex circleRegex(R"(smoothCircle\s*\(\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-1])\s*\))");
    static const std::regex depthRegex(R"(moveDepth\s*\(\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*,\s*([0-9]+\.[0-9]+)\s*\))");
    
    std::smatch matches;
    
    // Try to match move operation
    if (std::regex_search(opStr, matches, moveRegex) && matches.size() == 3) {
        op.type = OperationType::MOVE;
        op.params.move.x = std::stof(matches[1].str());
        op.params.move.y = std::stof(matches[2].str());
        op.duration = 0.0f; // Instant
    }
    // Try to match rest operation
    else if (std::regex_search(opStr, matches, restRegex) && matches.size() == 2) {
        op.type = OperationType::REST;
        op.params.rest.seconds = std::stof(matches[1].str());
        op.duration = op.params.rest.seconds;
    }
    // Try to match smooth operation
    else if (std::regex_search(opStr, matches, smoothRegex) && matches.size() == 6) {
        op.type = OperationType::SMOOTH;
        op.params.smooth.x1 = std::stof(matches[1].str());
        op.params.smooth.y1 = std::stof(matches[2].str());
        op.params.smooth.x2 = std::stof(matches[3].str());
        op.params.smooth.y2 = std::stof(matches[4].str());
        op.params.smooth.seconds = std::stof(matches[5].str());
        op.duration = op.params.smooth.seconds;
    }
    // Try to match circle operation
    else if (std::regex_search(opStr, matches, circleRegex) && matches.size() == 6) {
        op.type = OperationType::SMOOTH_CIRCLE;
        op.params.circle.centerX = std::stof(matches[1].str());
        op.params.circle.centerY = std::stof(matches[2].str());
        op.params.circle.radius = std::stof(matches[3].str());
        op.params.circle.seconds = std::stof(matches[4].str());
        op.params.circle.clockwise = (std::stoi(matches[5].str()) == 1);
        op.duration = op.params.circle.seconds;
    }
    // Try to match depth operation
    else if (std::regex_search(opStr, matches, depthRegex) && matches.size() == 6) {
        op.type = OperationType::MOVE_AWAY_TOWARD;
        op.params.depth.x = std::stof(matches[1].str());
        op.params.depth.y = std::stof(matches[2].str());
        op.params.depth.startDistance = std::stof(matches[3].str());
        op.params.depth.endDistance = std::stof(matches[4].str());
        op.params.depth.seconds = std::stof(matches[5].str());
        op.duration = op.params.depth.seconds;
    }
    else {
        return false; // No match found
    }
    
    m_operations.push_back(op);
    return true;
}

TargetPosition RoutineController::step()
{
    // Initialize timing if this is the first call
    if (!m_routineStarted) {
        m_lastUpdateTime = std::chrono::high_resolution_clock::now();
        m_routineStarted = true;
    }
    
    // Calculate elapsed time since last update
    auto currentTime = std::chrono::high_resolution_clock::now();
    float deltaTime = std::chrono::duration<float>(currentTime - m_lastUpdateTime).count();
    m_lastUpdateTime = currentTime;

    m_elapsedTime += (double_t) deltaTime;
    RoutineController::m_globalAdvancedTime = m_elapsedTime;
    
    // If we have operations to process
    if (!m_operations.empty() && m_currentOpIndex < m_operations.size()) {
        Operation& currentOp = m_operations[m_currentOpIndex];
        
        // Update elapsed time for current operation
        currentOp.elapsedTime += deltaTime;
        
        // Check if we need to move to the next operation
        if (currentOp.elapsedTime >= currentOp.duration) {
            // Move to next operation
            m_currentOpIndex++;
            
            // If we have more operations, initialize the next one
            if (m_currentOpIndex < m_operations.size()) {
                currentOp = m_operations[m_currentOpIndex];
                currentOp.elapsedTime = 0.0f;
            }
        }
    }
    
    // Calculate current position based on active operation
    return calculatePosition();
}

TargetPosition RoutineController::calculatePosition()
{
    // Don't progress stages if routine is already complete
    if(isComplete()) {
        // Return a static position when complete
        m_currentPosition.pitch = 0.0f;
        m_currentPosition.yaw = 0.0f;
        m_currentPosition.distance = TARGET_DEFAULT_DISTANCE;
        m_currentPosition.state = FLAG_ROUTINE_COMPLETE;
        return m_currentPosition;
    }
    
    // Handle stage progression for stages 3 and above, but not if already complete
    if(RoutineController::m_routineStage >= 3 && RoutineController::m_routineStage <= MAX_ROUTINE_STAGE){
        handleStageProgression();
        
        // Handle convergence test stages
        if(RoutineController::m_routineStage >= CONVERGENCE_NOTIFY_STAGE && RoutineController::m_routineStage <= CONVERGENCE_STAGE){
            if(RoutineController::m_routineStage == CONVERGENCE_STAGE) {
                //printf("In convergence stage %d, calling calculateConvergencePosition()\n", CONVERGENCE_STAGE);
            }
            return calculateConvergencePosition();
        }
        
        // Handle dilation test stages
        if(RoutineController::m_routineStage >= DILATION_STAGE_START && RoutineController::m_routineStage <= DILATION_STAGE_END){
            //printf("In dilation stage %d\n", RoutineController::m_routineStage);
            return calculateDilationPosition();
        }
        
        // For other stages before convergence test, keep crosshair at center
        if(RoutineController::m_routineStage >= 3 && RoutineController::m_routineStage < CONVERGENCE_NOTIFY_STAGE){
            m_currentPosition.pitch = 0.0f;
            m_currentPosition.yaw = 0.0f;
            m_currentPosition.distance = TARGET_DEFAULT_DISTANCE;
            m_currentPosition.state = FLAG_IN_MOVEMENT;
            return m_currentPosition;
        }
    }

    // Don't run legacy S-pattern code if routine is complete
    if(isComplete()) {
        return m_currentPosition; // Position already set in early return above
    }
    
    // Initial calibration period
    if(m_elapsedTime <= TIME_BETWEEN_ROUTINES) {
        m_currentPosition.distance = TARGET_DEFAULT_DISTANCE;
        m_currentPosition.pitch = 0.0;
        m_currentPosition.yaw = 0.0;
        m_currentPosition.state = FLAG_IN_MOVEMENT;
        RoutineController::m_routineStage = 0;
        return m_currentPosition;
    }else if(RoutineController::m_routineStage == 0){
        beep(174, 500);
        RoutineController::m_routineStage = 1;
    }
    
    

    // S-pattern parameters
    float scanTime = (float)(m_elapsedTime - TIME_BETWEEN_ROUTINES);
    const float maxPitch = 32.0f;  // Vertical range
    const float maxYaw = 32.0f;    // Horizontal range
    const float moveSpeed = 12.0f;  // Units per second
    
    // Use parametric equations for a continuous S-curve
    // This approach uses a single time parameter to generate the entire path
    
    // Calculate total path time (arbitrary scaling to control speed)
    const float totalTime = 60.0f;  // seconds for one complete cycle

    if((m_elapsedTime - TIME_BETWEEN_ROUTINES) > totalTime && (m_elapsedTime - TIME_BETWEEN_ROUTINES) < totalTime + 5){
        scanTime = totalTime + 0.1f;
    }else if((m_elapsedTime - TIME_BETWEEN_ROUTINES) > totalTime + 5){
        scanTime = (float)(m_elapsedTime - (TIME_BETWEEN_ROUTINES + 5));
    }
    
    // Normalize time to 0-1 range for one complete cycle
    float t = fmod(scanTime, totalTime) / totalTime;
    
    // Number of complete S-curves in vertical direction
    const int numCycles = 2;  // 4 complete S-curves vertically
    
    // Parametric equations for S-curve
    // x = A * sin(2π * t)
    // y = B * t
    
    // Scale to fit our desired range
    float yaw = (maxYaw / 2) * sin(2 * M_PI * numCycles * t);
    float pitch = maxPitch * (t - 0.5f);  // Map t from [0,1] to pitch [-maxPitch/2, maxPitch/2]
    
    // Update position
    m_currentPosition.distance = TARGET_DEFAULT_DISTANCE;
    if(scanTime > totalTime * 2){
        // Transition to facial expression calibration after gaze calibration
        // Only transition if we're not already past stage 3 (i.e., don't interrupt completed routines)
        if(RoutineController::m_routineStage < 3) {
            RoutineController::m_routineStage = 3;
            RoutineController::m_stageStartTime = m_elapsedTime; // Initialize stage timer
            beep(174, 500);
            printf("Initial transition to stage 3 at time %.2f\n", m_elapsedTime);
        }
    }else if(scanTime > totalTime){
        RoutineController::m_routineStage = 2;
        m_currentPosition.pitch = yaw;
        m_currentPosition.yaw = pitch;
    //}else if(scanTime > totalTime){
        //float terp = (scanTime-totalTime) / 2;
        //float newYaw = (pitch * terp) + yaw * (1-terp);
        //float newPitch = (yaw * terp) + pitch * (1-terp);
    }else{
        RoutineController::m_routineStage = 1;
        m_currentPosition.pitch = pitch;
        m_currentPosition.yaw = yaw;
    }
    
    m_currentPosition.state = FLAG_IN_MOVEMENT;
    
    return m_currentPosition;
}

TargetPosition RoutineController::screenToAngles(float x, float y) const
{
    // Convert normalized screen coordinates (0-1) to yaw and pitch angles
    // Center (0.5, 0.5) corresponds to (0, 0) in angles
    TargetPosition pos;
    
    // Map x from 0-1 to -MAX_YAW_ANGLE to +MAX_YAW_ANGLE
    pos.yaw = (x - 0.5f) * 2.0f * MAX_YAW_ANGLE;
    
    // Map y from 0-1 to -MAX_PITCH_ANGLE to +MAX_PITCH_ANGLE
    // Note: y=0 is top of screen, but pitch is positive upward, so we invert
    pos.pitch = (0.5f - y) * 2.0f * MAX_PITCH_ANGLE;
    
    // Set default distance
    pos.distance = TARGET_DEFAULT_DISTANCE;
    
    // Initialize state to 0
    pos.state = 0;
    
    return pos;
}

void RoutineController::reset()
{
    m_currentOpIndex = 0;
    m_routineStarted = false;
    
    // Reset elapsed time for all operations
    for (auto& op : m_operations) {
        op.elapsedTime = 0.0f;
    }
    
    // Reset position to center if we have operations
    if (!m_operations.empty()) {
        // Find the first move operation to set initial position
        for (const auto& op : m_operations) {
            if (op.type == OperationType::MOVE) {
                m_currentPosition = screenToAngles(op.params.move.x, op.params.move.y);
                break;
            }
        }
    }
}

bool RoutineController::isComplete() const
{
    // Routine is complete when we've finished all stages (beyond MAX_ROUTINE_STAGE)
    return RoutineController::m_routineStage > MAX_ROUTINE_STAGE;
}

size_t RoutineController::getCurrentOperationIndex() const
{
    return m_currentOpIndex;
}

size_t RoutineController::getTotalOperationCount() const
{
    return m_operations.size();
}

std::vector<std::string> RoutineController::getRoutineNames()
{
    // Return the predefined routine names
    const char* names[] = ALL_ROUTINE_NAMES;
    return std::vector<std::string>(names, names + NUM_CALIBRATION_ROUTINES);
}

TargetPosition RoutineController::calculateConvergencePosition()
{
    // Convergence test parameters
    const float minDistance = 0.19f; // Minimum distance (close to face)
    const float maxDistance = 1.5f; // Maximum distance (far from face)
    const float cycleTime = 4.0f; // 4 seconds for one complete in-out cycle
    
    // Notification stage (handled by main stage progression system)
    if(RoutineController::m_routineStage == CONVERGENCE_NOTIFY_STAGE) {
        // Keep crosshair at center, default distance during countdown
        m_currentPosition.pitch = 0.0f;
        m_currentPosition.yaw = 0.0f;
        m_currentPosition.distance = TARGET_DEFAULT_DISTANCE;
        m_currentPosition.state = FLAG_IN_MOVEMENT;
        return m_currentPosition;
    }
    
    // Active convergence test
    if(RoutineController::m_routineStage == CONVERGENCE_STAGE) {
        // Calculate elapsed time in current stage (not total elapsed time)
        float testTime = (float)(m_elapsedTime - RoutineController::m_stageStartTime);
        printf("Stage %d: testTime=%.2f, stageStartTime=%.2f, totalElapsed=%.2f\n", 
               CONVERGENCE_STAGE, testTime, RoutineController::m_stageStartTime, m_elapsedTime);
        
        // Calculate distance using smooth sinusoidal animation
        // This creates a smooth in-out motion that tests convergence
        float cycleProgress = fmod(testTime, cycleTime) / cycleTime; // 0-1 over cycle
        float distanceValue = sin(cycleProgress * 2.0f * M_PI); // -1 to 1
        
        // Map sine wave to distance range (close when sin is negative, far when positive)
        float distance = minDistance + (maxDistance - minDistance) * (distanceValue + 1.0f) / 2.0f;
        
        // Keep crosshair centered for convergence test
        m_currentPosition.pitch = 0.0f;
        m_currentPosition.yaw = 0.0f;
        m_currentPosition.distance = distance;
        m_currentPosition.state = FLAG_IN_MOVEMENT;
        return m_currentPosition;
    }
    
    // Fallback
    m_currentPosition.distance = TARGET_DEFAULT_DISTANCE;
    m_currentPosition.state = FLAG_IN_MOVEMENT;
    return m_currentPosition;
}

TargetPosition RoutineController::calculateDilationPosition()
{
    // Keep crosshair centered during all dilation stages
    m_currentPosition.pitch = 0.0f;
    m_currentPosition.yaw = 0.0f;
    m_currentPosition.distance = TARGET_DEFAULT_DISTANCE;
    
    // Set special state flags to control overlay rendering
    if(RoutineController::m_routineStage == DILATION_BLACK_STAGE) {
        // Black screen for full dilation
        m_currentPosition.state = FLAG_DILATION_BLACK;
        printf("Stage %d: Setting FLAG_DILATION_BLACK\n", DILATION_BLACK_STAGE);
    } else if(RoutineController::m_routineStage == DILATION_WHITE_STAGE) {
        // White screen for full constriction
        m_currentPosition.state = FLAG_DILATION_WHITE;
        printf("Stage %d: Setting FLAG_DILATION_WHITE\n", DILATION_WHITE_STAGE);
    } else if(RoutineController::m_routineStage == DILATION_GRADIENT_STAGE) {
        // Gradient fade from white to black
        float testTime = (float)(m_elapsedTime - RoutineController::m_stageStartTime);
        float fadeProgress = testTime / DILATION_GRADIENT_DURATION; // 0.0 to 1.0
        fadeProgress = (fadeProgress < 0.0f) ? 0.0f : (fadeProgress > 1.0f) ? 1.0f : fadeProgress; // Clamp to [0,1]
        
        // Set fade progress in the overlay manager static variable
        OverlayManager::s_routineFadeProgress = fadeProgress; // 0.0 = white, 1.0 = black
        m_currentPosition.state = FLAG_DILATION_GRADIENT;
        printf("Stage %d: Setting FLAG_DILATION_GRADIENT, progress=%.2f\n", DILATION_GRADIENT_STAGE, fadeProgress);
    } else {
        // Notification stages - maintain dilation state for pupil consistency
        if(RoutineController::m_routineStage == DILATION_NOTIFY_1_STAGE) {
            // Before black screen - maintain neutral/previous state
            m_currentPosition.state = FLAG_IN_MOVEMENT;
            printf("Stage %d: Pre-black screen notification\n", DILATION_NOTIFY_1_STAGE);
        } else if(RoutineController::m_routineStage == DILATION_NOTIFY_2_STAGE) {
            // After black screen, before white - maintain black to keep pupils dilated
            m_currentPosition.state = FLAG_DILATION_BLACK;
            printf("Stage %d: Maintaining black screen for pupil consistency\n", DILATION_NOTIFY_2_STAGE);
        } else if(RoutineController::m_routineStage == DILATION_NOTIFY_3_STAGE) {
            // After white screen, before fade - maintain white to keep pupils constricted
            m_currentPosition.state = FLAG_DILATION_WHITE;
            printf("Stage %d: Maintaining white screen for pupil consistency\n", DILATION_NOTIFY_3_STAGE);
        } else {
            // Fallback for any other notification stages
            m_currentPosition.state = FLAG_IN_MOVEMENT;
            printf("Stage %d: Default notification stage\n", RoutineController::m_routineStage);
        }
    }
    
    return m_currentPosition;
}

void RoutineController::handleStageProgression()
{
    // Don't progress if already complete
    if(RoutineController::m_routineStage > MAX_ROUTINE_STAGE) {
        return;
    }
    
    // Calculate time elapsed in current stage
    double_t stageElapsed = m_elapsedTime - RoutineController::m_stageStartTime;
    
    // Debug output
    static int lastDebugStage = -1;
    if (lastDebugStage != RoutineController::m_routineStage) {
        printf("Stage %d: elapsed=%.2f, stageStart=%.2f, totalElapsed=%.2f\n", 
               RoutineController::m_routineStage, stageElapsed, RoutineController::m_stageStartTime, m_elapsedTime);
        lastDebugStage = RoutineController::m_routineStage;
    }
    
    // Determine if current stage should advance
    bool shouldAdvance = false;
    float stageDuration = 0.0f;
    
    // Notification stages (odd numbers 3, 5, 7, 9, 11, 13, CONVERGENCE_NOTIFY_STAGE, DILATION_NOTIFY_*): countdown stages
    // Exclude completion stage from normal progression
    if(RoutineController::m_routineStage % 2 == 1 && RoutineController::m_routineStage >= 3 && RoutineController::m_routineStage < COMPLETION_STAGE) {
        stageDuration = STAGE_NOTIFICATION_DURATION;
        shouldAdvance = (stageElapsed >= stageDuration);
    }
    // Action stages (even numbers 4, 6, 8, 10, 12, 14, CONVERGENCE_STAGE, DILATION_BLACK_STAGE, DILATION_WHITE_STAGE, DILATION_GRADIENT_STAGE): user action stages  
    else if(RoutineController::m_routineStage % 2 == 0 && RoutineController::m_routineStage >= 4) {
        // Special durations for different test types
        if(RoutineController::m_routineStage == CONVERGENCE_STAGE) {
            stageDuration = CONVERGENCE_TEST_DURATION;
            printf("Stage %d (convergence): elapsed=%.2f, duration=%.2f, shouldAdvance=%d\n", 
                   CONVERGENCE_STAGE, stageElapsed, stageDuration, (stageElapsed >= stageDuration));
        } else if(RoutineController::m_routineStage == DILATION_BLACK_STAGE || RoutineController::m_routineStage == DILATION_WHITE_STAGE) {
            stageDuration = DILATION_ACTION_DURATION; // Black/white screens
        } else if(RoutineController::m_routineStage == DILATION_GRADIENT_STAGE) {
            stageDuration = DILATION_GRADIENT_DURATION; // Gradient fade
            //printf("STAGE22_DEBUG: elapsed=%.2f, duration=%.2f, shouldAdvance=%d\n", 
            //       stageElapsed, stageDuration, (stageElapsed >= stageDuration));
        } else {
            stageDuration = STAGE_ACTION_DURATION;
        }
        shouldAdvance = (stageElapsed >= stageDuration);
    }
    
    // Advance to next stage if time is up
    if(shouldAdvance) {
        RoutineController::m_routineStage++;
        RoutineController::m_stageStartTime = m_elapsedTime; // Reset stage timer
        RoutineController::m_stepWritten = false; // Reset step written flag for new stage
        
        printf("Advanced to stage %d at time %.2f\n", RoutineController::m_routineStage, m_elapsedTime);
        
        // End routine after dilation test
        if(RoutineController::m_routineStage > MAX_ROUTINE_STAGE) {
            printf("COMPLETION_DEBUG: Stage %d > MAX_ROUTINE_STAGE(%d), setting to COMPLETION_STAGE(%d)\n", 
                   RoutineController::m_routineStage, MAX_ROUTINE_STAGE, COMPLETION_STAGE);
            RoutineController::m_routineStage = COMPLETION_STAGE; // Mark as complete
            OverlayManager::s_routineState = FLAG_ROUTINE_COMPLETE;
            printf("COMPLETION_DEBUG: Set FLAG_ROUTINE_COMPLETE\n");
        } else {
            // Only beep for stage transitions, not for completion
            beep(174, 500); // Audio feedback for stage transitions
        }
    }
}