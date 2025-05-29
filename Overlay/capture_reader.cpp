#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <tuple>
#include <turbojpeg.h>
#include "capture_data.h"
#include "capture_reader.h"

struct PotentialMatch {
    uint64_t label_ts;
    std::tuple<float, float, float, float, float, float, float, float, float, float, float, uint32_t> label_data;
    std::tuple<size_t, uint64_t, std::vector<uint8_t>> left_match;
    std::tuple<size_t, uint64_t, std::vector<uint8_t>> right_match;
    uint64_t quality;
};

std::vector<AlignedFrame> read_capture_file(const std::string& filename) {
    // Store all frames without assuming alignment
    std::map<uint64_t, std::vector<uint8_t>> all_eye_frames_left;  // video_timestamp_left -> image_data
    std::map<uint64_t, std::vector<uint8_t>> all_eye_frames_right; // video_timestamp_right -> image_data
    std::map<uint64_t, std::tuple<float, float, float, float, float, float, float, float, float, float, float, uint32_t>> all_label_frames; // timestamp -> label_data
    
    int raw_frames = 0;
    
    // Read the raw data from file
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return {};
    }
    
    while (file.good()) {
        CaptureFrame frame;
        
        // Read the frame metadata
        if (!file.read(reinterpret_cast<char*>(&frame), sizeof(CaptureFrame))) {
            if (file.eof()) {
                std::cout << "Breaking - end of file reached" << std::endl;
            } else {
                std::cerr << "Error reading frame metadata" << std::endl;
            }
            break;
        }
        
        // Read the image data
        std::vector<uint8_t> image_left_data(frame.jpeg_data_left_length);
        std::vector<uint8_t> image_right_data(frame.jpeg_data_right_length);
        
        if (!file.read(reinterpret_cast<char*>(image_left_data.data()), frame.jpeg_data_left_length) ||
            !file.read(reinterpret_cast<char*>(image_right_data.data()), frame.jpeg_data_right_length)) {
            std::cerr << "Error reading image data" << std::endl;
            break;
        }
        
        raw_frames++;
        
        // Store all frame data
        all_eye_frames_left[frame.timestamp_left] = image_left_data;
        all_eye_frames_right[frame.timestamp_right] = image_right_data;
        all_label_frames[frame.timestamp] = std::make_tuple(
            frame.routinePitch, frame.routineYaw, frame.routineDistance, 
            frame.fovAdjustDistance, frame.routineLeftLid, frame.routineRightLid,
            frame.routineBrowRaise, frame.routineBrowAngry, frame.routineWiden, 
            frame.routineSquint, frame.routineDilate, frame.routineState
        );
        
        /*std::cout << "Read frame: Pitch=" << frame.routinePitch
                  << ", Yaw=" << frame.routineYaw
                  << ", sizeRight=" << image_right_data.size()
                  << ", sizeLeft=" << image_left_data.size()
                  << ", timeData=" << frame.timestamp
                  << ", timeLeft=" << frame.timestamp_left
                  << ", timeRight=" << frame.timestamp_right << std::endl;*/
    }
    
    std::cout << "Detected " << raw_frames << " raw frames" << std::endl;
    std::cout << "Unique left eye frames: " << all_eye_frames_left.size() << std::endl;
    std::cout << "Unique right eye frames: " << all_eye_frames_right.size() << std::endl;
    std::cout << "Unique label frames: " << all_label_frames.size() << std::endl;
    
    // Convert to sorted vectors for processing
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> left_frames;
    for (const auto& pair : all_eye_frames_left) {
        left_frames.push_back(pair);
    }
    std::sort(left_frames.begin(), left_frames.end());
    
    std::vector<std::pair<uint64_t, std::vector<uint8_t>>> right_frames;
    for (const auto& pair : all_eye_frames_right) {
        right_frames.push_back(pair);
    }
    std::sort(right_frames.begin(), right_frames.end());
    
    std::vector<std::pair<uint64_t, std::tuple<float, float, float, float, float, float, float, float, float, float, float, uint32_t>>> label_frames;
    for (const auto& pair : all_label_frames) {
        label_frames.push_back(pair);
    }
    std::sort(label_frames.begin(), label_frames.end());
    
    // Track which frames have been used
    std::set<size_t> used_left_frames;
    std::set<size_t> used_right_frames;
    
    // Store potential matches for each label
    std::vector<PotentialMatch> potential_matches;
    
    for (const auto& label_pair : label_frames) {
        uint64_t label_ts = label_pair.first;
        const auto& label_data = label_pair.second;
        
        // Find the best available left frame
        size_t best_left_idx = 0;
        uint64_t best_left_ts = 0;
        std::vector<uint8_t> best_left_img;
        uint64_t best_left_deviation = std::numeric_limits<uint64_t>::max();
        
        for (size_t left_idx = 0; left_idx < left_frames.size(); left_idx++) {
            if (used_left_frames.find(left_idx) != used_left_frames.end()) {
                continue;
            }
            
            uint64_t left_ts = left_frames[left_idx].first;
            uint64_t deviation = (left_ts > label_ts) ? (left_ts - label_ts) : (label_ts - left_ts);
            
            if (deviation < best_left_deviation) {
                best_left_deviation = deviation;
                best_left_idx = left_idx;
                best_left_ts = left_ts;
                best_left_img = left_frames[left_idx].second;
            }
        }
        
        // Find the best available right frame
        size_t best_right_idx = 0;
        uint64_t best_right_ts = 0;
        std::vector<uint8_t> best_right_img;
        uint64_t best_right_deviation = std::numeric_limits<uint64_t>::max();
        
        for (size_t right_idx = 0; right_idx < right_frames.size(); right_idx++) {
            if (used_right_frames.find(right_idx) != used_right_frames.end()) {
                continue;
            }
            
            uint64_t right_ts = right_frames[right_idx].first;
            uint64_t deviation = (right_ts > label_ts) ? (right_ts - label_ts) : (label_ts - right_ts);
            
            if (deviation < best_right_deviation) {
                best_right_deviation = deviation;
                best_right_idx = right_idx;
                best_right_ts = right_ts;
                best_right_img = right_frames[right_idx].second;
            }
        }
        
        // Store this potential match with its quality metric (sum of deviations)
        if (!best_left_img.empty() && !best_right_img.empty()) {
            uint64_t match_quality = best_left_deviation + best_right_deviation;
            
            PotentialMatch match;
            match.label_ts = label_ts;
            match.label_data = label_data;
            match.left_match = std::make_tuple(best_left_idx, best_left_ts, best_left_img);
            match.right_match = std::make_tuple(best_right_idx, best_right_ts, best_right_img);
            match.quality = match_quality;
            
            potential_matches.push_back(match);
        }
    }
    
    // Sort potential matches by quality (lower is better)
    std::sort(potential_matches.begin(), potential_matches.end(), 
              [](const PotentialMatch& a, const PotentialMatch& b) {
                  return a.quality < b.quality;
              });
    
    // Now select the best matches while ensuring no frame is used twice
    std::vector<AlignedFrame> final_frames;
    
    for (const auto& match : potential_matches) {
        size_t left_idx = std::get<0>(match.left_match);
        size_t right_idx = std::get<0>(match.right_match);
        
        // Skip if either frame has already been used
        if (used_left_frames.find(left_idx) != used_left_frames.end() || 
            used_right_frames.find(right_idx) != used_right_frames.end()) {
            continue;
        }
        
        // Mark these frames as used
        used_left_frames.insert(left_idx);
        used_right_frames.insert(right_idx);
        
        // Add to final frames
        AlignedFrame aligned_frame;
        aligned_frame.label_data = match.label_data;
        aligned_frame.left_image = std::get<2>(match.left_match);
        aligned_frame.right_image = std::get<2>(match.right_match);
        aligned_frame.label_timestamp = match.label_ts;
        
        final_frames.push_back(aligned_frame);
    }
    
    // Sort final frames by label timestamp for consistency
    std::sort(final_frames.begin(), final_frames.end(), 
              [](const AlignedFrame& a, const AlignedFrame& b) {
                  return a.label_timestamp < b.label_timestamp;
              });
    
    // Calculate final statistics
    if (!final_frames.empty()) {
        uint64_t total_left_deviation = 0;
        uint64_t total_right_deviation = 0;
        
        for (const auto& frame : final_frames) {
            uint64_t label_ts = frame.label_timestamp;
            
            // Find the timestamps for these images
            uint64_t left_ts = 0;
            uint64_t right_ts = 0;
            
            for (const auto& left_pair : left_frames) {
                if (left_pair.second == frame.left_image) {
                    left_ts = left_pair.first;
                    break;
                }
            }
            
            for (const auto& right_pair : right_frames) {
                if (right_pair.second == frame.right_image) {
                    right_ts = right_pair.first;
                    break;
                }
            }
            
            total_left_deviation += (left_ts > label_ts) ? (left_ts - label_ts) : (label_ts - left_ts);
            total_right_deviation += (right_ts > label_ts) ? (right_ts - label_ts) : (label_ts - right_ts);
        }
        
        double avg_left_deviation = static_cast<double>(total_left_deviation) / final_frames.size();
        double avg_right_deviation = static_cast<double>(total_right_deviation) / final_frames.size();
        
        std::cout << "Aligned " << final_frames.size() << " frames" << std::endl;
        std::cout << "Average deviation: left: " << avg_left_deviation << "ms, right: " 
                  << avg_right_deviation << "ms" << std::endl;
    } else {
        std::cout << "No frames could be aligned" << std::endl;
    }
    
    return final_frames;
}

bool AlignedFrame::DecodeImageLeft(std::vector<uint32_t>& rgb_buffer, int& width, int& height) const {
    // Check if we have cached data
    if (has_decoded_left) {
        // Use cached data
        width = left_width;
        height = left_height;
        
        // Resize the output buffer if needed
        if (rgb_buffer.size() != static_cast<unsigned>(width * height)) {
            rgb_buffer.resize(width * height);
        }
        
        // Copy the cached data to the output buffer
        std::copy(rgb_buffer_left.begin(), rgb_buffer_left.end(), rgb_buffer.begin());
        return true;
    }
    
    // No cached data, perform the decode
    bool result = DecodeJpegData(left_image, rgb_buffer, width, height);
    
    if (result) {
        // Cache the results for future use by making a new copy
        left_width = width;
        left_height = height;
        rgb_buffer_left.resize(width * height);
        std::copy(rgb_buffer.begin(), rgb_buffer.end(), rgb_buffer_left.begin());
        has_decoded_left = true;
    }
    
    return result;
}

// Decode the right eye image to RGB pixels with caching
bool AlignedFrame::DecodeImageRight(std::vector<uint32_t>& rgb_buffer, int& width, int& height) const {
    // Check if we have cached data
    if (has_decoded_right) {
        // Use cached data
        width = right_width;
        height = right_height;
        
        // Resize the output buffer if needed
        if (rgb_buffer.size() != static_cast<unsigned>(width * height)) {
            rgb_buffer.resize(width * height);
        }
        
        // Copy the cached data to the output buffer
        std::copy(rgb_buffer_right.begin(), rgb_buffer_right.end(), rgb_buffer.begin());
        return true;
    }
    
    // No cached data, perform the decode
    bool result = DecodeJpegData(right_image, rgb_buffer, width, height);
    
    if (result) {
        // Cache the results for future use by making a new copy
        right_width = width;
        right_height = height;
        rgb_buffer_right.resize(width * height);
        std::copy(rgb_buffer.begin(), rgb_buffer.end(), rgb_buffer_right.begin());
        has_decoded_right = true;
    }
    
    return result;
}

// Helper method for JPEG decoding using libturbojpeg
bool AlignedFrame::DecodeJpegData(const std::vector<uint8_t>& jpeg_data, 
                                 std::vector<uint32_t>& pixel_buffer, 
                                 int& width, 
                                 int& height) const {
    if (jpeg_data.empty()) {
        return false;
    }

    // Create a TurboJPEG decompressor instance
    tjhandle tjInstance = tjInitDecompress();
    if (tjInstance == NULL) {
        return false;
    }

    // Get the dimensions from the JPEG header
    int jpegSubsamp, jpegColorspace;
    int result = tjDecompressHeader3(
        tjInstance,
        jpeg_data.data(),
        (unsigned long)jpeg_data.size(),
        &width,
        &height,
        &jpegSubsamp,
        &jpegColorspace
    );

    if (result != 0) {
        tjDestroy(tjInstance);
        return false;
    }

    // Resize the output buffer
    pixel_buffer.resize(width * height);
    
    // Option 1: Direct decompression to memory (most efficient)
    // This assumes pixel_buffer is contiguous memory that can be safely
    // reinterpreted as unsigned char* for the RGB data
    unsigned char* rgb_ptr = reinterpret_cast<unsigned char*>(pixel_buffer.data());
    
    result = tjDecompress2(
        tjInstance,
        jpeg_data.data(),
        (unsigned long)jpeg_data.size(),
        rgb_ptr,
        width,
        width * 4,  // pitch: bytes per line including padding
        height,
        TJPF_RGBX,  // RGBX format with 4 bytes per pixel (ignoring X)
        TJFLAG_FASTDCT
    );

    // If the direct approach doesn't work, fall back to a two-step process
    if (result != 0) {
        // Allocate temporary RGB buffer
        std::vector<unsigned char> rgb_buffer(width * height * 3);
        
        result = tjDecompress2(
            tjInstance,
            jpeg_data.data(),
            (unsigned long)jpeg_data.size(),
            rgb_buffer.data(),
            width,
            0,
            height,
            TJPF_RGB,
            TJFLAG_FASTDCT
        );
        
        if (result == 0) {
            // More efficient packing using pointer arithmetic
            const unsigned char* src = rgb_buffer.data();
            for (int i = 0; i < width * height; i++) {
                // Pack as 0x00RRGGBB
                pixel_buffer[i] = (src[0] << 16) | (src[1] << 8) | src[2];
                src += 3;
            }
        }
    }

    tjDestroy(tjInstance);
    return (result == 0);
}
