
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <turbojpeg.h>  // libjpeg-turbo

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET socket_t;
  #define SOCKET_INVALID INVALID_SOCKET
  #define CLOSE_SOCKET closesocket
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  typedef int socket_t;
  #define SOCKET_INVALID -1
  #define CLOSE_SOCKET close
#endif

#define BUFFER_SIZE 8192
#define MAX_URL_LEN 256

typedef struct {
    socket_t sock;
    char host[MAX_URL_LEN];
    char path[MAX_URL_LEN];
    int port;
    char boundary[256];
    char buffer[BUFFER_SIZE];
    size_t buffer_pos;
    size_t buffer_len;
    tjhandle tjInstance;
} MJPEGStream;

// Initialize socket subsystem
void init_sockets() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

// Clean up socket subsystem
void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// Parse URL into host, path, and port
int parse_url(const char* url, char* host, char* path, int* port) {
    if (strncmp(url, "http://", 7) != 0) {
        fprintf(stderr, "Only HTTP protocol is supported\n");
        return 0;
    }
    
    const char* host_start = url + 7;
    const char* path_start = strchr(host_start, '/');
    
    if (!path_start) {
        strncpy(host, host_start, MAX_URL_LEN - 1);
        host[MAX_URL_LEN - 1] = '\0';
        strcpy(path, "/");
    } else {
        size_t host_len = path_start - host_start;
        strncpy(host, host_start, host_len);
        host[host_len] = '\0';
        strncpy(path, path_start, MAX_URL_LEN - 1);
        path[MAX_URL_LEN - 1] = '\0';
    }
    
    // Check for port in host
    char* port_str = strchr(host, ':');
    if (port_str) {
        *port_str = '\0';
        *port = atoi(port_str + 1);
    } else {
        *port = 80;  // Default HTTP port
    }
    
    return 1;
}

// Connect to the server
socket_t connect_to_server(const char* host, int port) {
    struct addrinfo hints, *res, *res0;
    socket_t sock = SOCKET_INVALID;
    char port_str[8];
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    
    snprintf(port_str, sizeof(port_str), "%d", port);
    
    if (getaddrinfo(host, port_str, &hints, &res0) != 0) {
        return SOCKET_INVALID;
    }
    
    for (res = res0; res; res = res->ai_next) {
        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock == SOCKET_INVALID) {
            continue;
        }
        
        if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == 0) {
            break;  // Connected successfully
        }
        
        CLOSE_SOCKET(sock);
        sock = SOCKET_INVALID;
    }
    
    freeaddrinfo(res0);
    return sock;
}

// Fill the buffer with data from the socket
int fill_buffer(MJPEGStream* stream) {
    if (stream->buffer_pos > 0 && stream->buffer_len > 0) {
        // Move remaining data to the beginning of the buffer
        memmove(stream->buffer, stream->buffer + stream->buffer_pos, 
                stream->buffer_len - stream->buffer_pos);
        stream->buffer_len -= stream->buffer_pos;
        stream->buffer_pos = 0;
    } else {
        stream->buffer_len = 0;
        stream->buffer_pos = 0;
    }
    
    int bytes_read = recv(stream->sock, stream->buffer + stream->buffer_len, 
                          BUFFER_SIZE - (int)stream->buffer_len, MSG_WAITALL);
    if (bytes_read <= 0) {
        return 0;  // Error or connection closed
    }
    
    stream->buffer_len += bytes_read;
    return 1;
}

// Find boundary in the buffer
char* find_boundary(MJPEGStream* stream) {
    if (!stream->boundary[0]) {
        return NULL;
    }
    
    char* pos = stream->buffer + stream->buffer_pos;
    char* end = stream->buffer + stream->buffer_len;
    size_t boundary_len = strlen(stream->boundary);
    
    while (pos + boundary_len <= end) {
        if (memcmp(pos, stream->boundary, boundary_len) == 0) {
            return pos;
        }
        pos++;
    }
    
    return NULL;
}

// Extract boundary from Content-Type header
int extract_boundary(MJPEGStream* stream, const char* header) {
    // Debug output
    printf("extract_boundary: Analyzing header: %.100s\n", header);
    
    const char* boundary_marker = "boundary=";
    const char* boundary_start = strstr(header, boundary_marker);
    
    if (!boundary_start) {
        printf("ERROR: No 'boundary=' found in header\n");
        return 0;
    }
    
    boundary_start += strlen(boundary_marker);
    printf("extract_boundary: Found boundary marker at offset %ld\n", (long)(boundary_start - header));
    
    // Skip any quotes
    if (*boundary_start == '"') {
        boundary_start++;
    }
    
    // Copy boundary
    strcpy(stream->boundary, "--");  // MJPEG boundaries start with --
    size_t i = 2;
    while (*boundary_start && *boundary_start != '"' && *boundary_start != '\r' && *boundary_start != '\n' && *boundary_start != ';') {
        if (i < sizeof(stream->boundary) - 1) {
            stream->boundary[i++] = *boundary_start;
        }
        boundary_start++;
    }
    stream->boundary[i] = '\0';
    
    // Verify we got a non-empty boundary
    if (i <= 2) {
        printf("ERROR: Empty boundary after '--'\n");
        return 0;
    }
    
    printf("extract_boundary: Extracted boundary: '%s'\n", stream->boundary);
    return 1;
}

// Find end of HTTP headers
char* find_headers_end(char* buffer, size_t len) {
    for (size_t i = 0; i < len - 3; i++) {
        if (buffer[i] == '\r' && buffer[i+1] == '\n' && 
            buffer[i+2] == '\r' && buffer[i+3] == '\n') {
            return buffer + i + 4;
        }
    }
    return NULL;
}

// Get MJPEG stream handle
MJPEGStream* GetStreamHandle(const char* url) {
    printf("GetStreamHandle: Starting with URL: %s\n", url);
    init_sockets();
    
    MJPEGStream* stream = (MJPEGStream*)malloc(sizeof(MJPEGStream));
    if (!stream) {
        return NULL;
    }
    
    memset(stream, 0, sizeof(MJPEGStream));
    stream->sock = SOCKET_INVALID;
    
    // Parse URL
    if (!parse_url(url, stream->host, stream->path, &stream->port)) {
        printf("ERROR: Failed to parse URL: %s\n", url);
        free(stream);
        return NULL;
    }
    printf("GetStreamHandle: URL parsed - Host: %s, Path: %s, Port: %d\n", 
           stream->host, stream->path, stream->port);
    
    // Connect to server
    stream->sock = connect_to_server(stream->host, stream->port);
    if (stream->sock == SOCKET_INVALID) {
        printf("ERROR: Failed to connect to server\n");
        free(stream);
        return NULL;
    }
    
    // Send HTTP request
    char request[1024];
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: keep-alive\r\n\r\n",
             stream->path, stream->host);
    
    printf("GetStreamHandle: Sending HTTP request:\n%s\n", request);
    send(stream->sock, request, (int)strlen(request), 0);
    
    // Read HTTP response headers
    printf("GetStreamHandle: Reading HTTP response headers...\n");
    char headers[8192] = {0};
    unsigned headers_len = 0;
    int headers_complete = 0;
    
    while (!headers_complete && headers_len < sizeof(headers) - 1) {
        char buf[1024];
        int received = recv(stream->sock, buf, sizeof(buf) - 1, MSG_WAITALL);
        
        if (received <= 0) {
            printf("ERROR: Failed to receive HTTP headers\n");
            CLOSE_SOCKET(stream->sock);
            free(stream);
            return NULL;
        }
        
        // Append to headers buffer
        if (headers_len + received < sizeof(headers)) {
            memcpy(headers + headers_len, buf, received);
            headers_len += received;
            headers[headers_len] = '\0';
        }
        
        // Check if we've received the end of headers
        if (strstr(headers, "\r\n\r\n")) {
            headers_complete = 1;
        }
    }
    
    printf("GetStreamHandle: Received HTTP headers (%d bytes):\n%.200s...\n", 
           headers_len, headers);
    
    // Find Content-Type header
    const char* content_type = NULL;
    const char* content_type_patterns[] = {"Content-Type:", "content-type:"};
    for (int i = 0; i < 2; i++) {
        content_type = strstr(headers, content_type_patterns[i]);
        if (content_type) {
            printf("GetStreamHandle: Found content-type header: %.50s\n", content_type);
            break;
        }
    }

    if (!content_type) {
        printf("GetStreamHandle: ERROR - No Content-Type header found\n");
        CLOSE_SOCKET(stream->sock);
        free(stream);
        return NULL;
    }
    
    // Find end of Content-Type header line
    char* content_type_end = strstr(content_type, "\r\n");
    if (content_type_end) {
        *content_type_end = '\0';  // Temporarily terminate string
        printf("GetStreamHandle: Found Content-Type: %s\n", content_type);
        *content_type_end = '\r';  // Restore original character
    }
    
    // Check if it's a multipart/x-mixed-replace content type
    if (!strstr(content_type, "multipart/x-mixed-replace")) {
        printf("ERROR: Not a multipart/x-mixed-replace stream\n");
        CLOSE_SOCKET(stream->sock);
        free(stream);
        return NULL;
    }
    
    // Extract boundary
    if (!extract_boundary(stream, content_type)) {
        printf("ERROR: Failed to extract boundary\n");
        CLOSE_SOCKET(stream->sock);
        free(stream);
        return NULL;
    }
    printf("init buffer...\n");
    // Find the end of headers and initialize buffer
    char* headers_end = strstr(headers, "\r\n\r\n");
    if (headers_end) {
        headers_end += 4;  // Skip \r\n\r\n
        
        // Copy remaining data to stream buffer
        int remaining = headers_len - (int)(headers_end - headers);
        if (remaining > 0) {
            memcpy(stream->buffer, headers_end, remaining);
            stream->buffer_len = remaining;
        }
    }
    printf("init tjpeg...\n");
    // Initialize TurboJPEG decompressor
    stream->tjInstance = tjInitDecompress();
    if (!stream->tjInstance) {
        printf("ERROR: Failed to initialize TurboJPEG decompressor\n");
        CLOSE_SOCKET(stream->sock);
        free(stream);
        return NULL;
    }
    
    printf("GetStreamHandle: Stream successfully initialized with boundary: %s\n", 
           stream->boundary);
    return stream;
}

// Decode a frame from the MJPEG stream
unsigned char* DecodeFrame(MJPEGStream* stream, int* width, int* height, uint64_t* timestamp, size_t* return_size) {
    //printf("DecodeFrame...\n");
    if (!stream || stream->sock == SOCKET_INVALID) {
        printf("cant decode frame: invalid stream state!\n");
        return NULL;
    }

    uint64_t timestamp_read = 0L;

    //printf("DecodeFrame Findboundary...\n");
    // Find boundary
    char* boundary_pos = NULL;
    while (!(boundary_pos = find_boundary(stream))) {
        if (!fill_buffer(stream)) {
            printf("cant decode frame: cant fill buffer\n");
            return NULL;
        }
    }
    //printf("DecodeFrame MoveBoundary...\n");
    // Move past the boundary
    stream->buffer_pos = (boundary_pos - stream->buffer) + strlen(stream->boundary);
    
    // Check for chunk encoding marker (hex digits followed by CRLF)
    int is_chunked = 0;
    size_t chunk_header_end = 0;
    
    // Look ahead to see if we have a hex number followed by CRLF
    for (size_t i = stream->buffer_pos; i < stream->buffer_len - 1; i++) {
        char c = stream->buffer[i];
        // Check if we have digits or a-f/A-F (hex)
        if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
            continue;
        } else if (c == '\r' && stream->buffer[i+1] == '\n') {
            // Found CRLF after hex digits - this is likely chunked encoding
            is_chunked = 1;
            chunk_header_end = i + 2; // Position after CRLF
            //printf("Detected chunked encoding format\n");
            break;
        } else {
            // Not chunked encoding
            break;
        }
    }
    
    // Handle chunked encoding
    if (is_chunked) {
        // Skip the chunk size and CRLF
        stream->buffer_pos = chunk_header_end;
        
        // Find Content-Type and Content-Length headers
        char* headers_end = NULL;
        // int content_length = -1;
        
        while (1) {
            if (stream->buffer_pos + 4 >= stream->buffer_len) {
                if (!fill_buffer(stream)) {
                    printf("cant decode frame: cant fill buffer(2)\n");
                    return NULL;
                }
            }
            
            headers_end = strstr(stream->buffer + stream->buffer_pos, "\r\n\r\n");
            if (headers_end) {
                *headers_end = '\0';  // Temporarily null-terminate for strstr
                
                char* content_length_header = strstr(stream->buffer + stream->buffer_pos, "Content-Length:");
                char* timestamp_header = strstr(stream->buffer + stream->buffer_pos, "X-Timestamp:");

                if (content_length_header) {
                    // content_length = atoi(content_length_header + 16);
                    //printf("Found Content-Length: %d\n", content_length);
                }

                if(timestamp_header){
                    //printf("Found timestamp: ");
                    //printf(timestamp_header);
                    //printf("\n");
                    timestamp_read = atoll(timestamp_header + 13);
                }
                
                *headers_end = '\r';  // Restore the character
                headers_end += 4;     // Move past \r\n\r\n
                break;
            }
            
            if (!fill_buffer(stream)) {
                printf("cant decode frame: cant fill buffer(3)\n");
                return NULL;
            }
        }
        
        // Update buffer position to after headers
        stream->buffer_pos = headers_end - stream->buffer;
        
        // Look for next chunk marker (hex followed by CRLF)
        size_t next_chunk_pos = stream->buffer_pos;
        int found_chunk_marker = 0;
        
        while (next_chunk_pos < stream->buffer_len - 1) {
            if (stream->buffer[next_chunk_pos] == '\r' && 
                stream->buffer[next_chunk_pos + 1] == '\n') {
                found_chunk_marker = 1;
                next_chunk_pos += 2; // Move past CRLF
                break;
            }
            next_chunk_pos++;
        }
        
        if (!found_chunk_marker) {
            // Need more data
            if (!fill_buffer(stream)) {
                printf("cant decode frame: cant find next chunk marker\n");
                return NULL;
            }
        }
        
        // Parse the chunk size (hex)
        char chunk_size_str[16] = {0};
        size_t i = 0;
        
        while (next_chunk_pos < stream->buffer_len && i < 15) {
            char c = stream->buffer[next_chunk_pos++];
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
                chunk_size_str[i++] = c;
            } else if (c == '\r' && next_chunk_pos < stream->buffer_len && 
                      stream->buffer[next_chunk_pos] == '\n') {
                next_chunk_pos++; // Skip the '\n'
                break;
            } else {
                break;
            }
        }
        
        // Convert hex string to integer
        unsigned int jpeg_size = 0;
        sscanf(chunk_size_str, "%x", &jpeg_size);
        //printf("JPEG chunk size: %u (0x%s)\n", jpeg_size, chunk_size_str);
        
        if (jpeg_size == 0 || jpeg_size > 10000000) { // Sanity check
            //printf("Invalid JPEG chunk size: %u\n", jpeg_size);
            return NULL;
        }
        
        // Allocate memory for JPEG data
        unsigned char* jpeg_data = (unsigned char*)malloc(jpeg_size);
        if (!jpeg_data) {
            printf("cant decode frame: jpeg malloc null\n");
            return NULL;
        }
        
        // Read JPEG data
        size_t total_read = 0;
        stream->buffer_pos = next_chunk_pos; // Start from after chunk size
        
        while (total_read < jpeg_size) {
            size_t available = stream->buffer_len - stream->buffer_pos;
            
            if (available == 0) {
                if (!fill_buffer(stream)) {
                    free(jpeg_data);
                    printf("cant decode frame: cant fill buffer for JPEG data\n");
                    return NULL;
                }
                available = stream->buffer_len - stream->buffer_pos;
            }
            
            size_t to_copy = ((jpeg_size - total_read) < available) ? 
                             (jpeg_size - total_read) : available;
            
            memcpy(jpeg_data + total_read, stream->buffer + stream->buffer_pos, to_copy);
            total_read += to_copy;
            stream->buffer_pos += to_copy;
        }
        
        // Decode JPEG data
        int jpegSubsamp, w, h;
        
        // Get the image dimensions first
        if (tjDecompressHeader2(stream->tjInstance, jpeg_data, jpeg_size, &w, &h, &jpegSubsamp) < 0) {
            //printf("Error decoding JPEG header: %s\n", tjGetErrorStr());
            
            // Debug dump
            //FILE* fp = fopen("./bad_data7.bin", "wb");
            //if (fp) {
            //    fwrite(jpeg_data, 1, jpeg_size, fp);
            //    fclose(fp);
            //    printf("Dumped bad JPEG data to bad_data4.bin (%u bytes)\n", jpeg_size);
            //}
            
            free(jpeg_data);
            return NULL;
        }

        //FILE* fp = fopen("./good_data.bin", "wb");
        //if (fp) {
        //    fwrite(jpeg_data, 1, jpeg_size, fp);
        //    fclose(fp);
        //    printf("Dumped bad JPEG data to bad_data4.bin (%u bytes)\n", jpeg_size);
        //}
        /*
        // Allocate pixel buffer (RGBA format)
        int* pixels = (int*)malloc(w * h * sizeof(int));
        if (!pixels) {
            free(jpeg_data);
            printf("cant decode frame: cant allocate pixel buffer\n");
            return NULL;
        }
        
        // Decode the JPEG
        if (tjDecompress2(stream->tjInstance, jpeg_data, jpeg_size, 
                         (unsigned char*)pixels, w, w * 4, h, TJPF_RGBA, 0) < 0) {
            free(jpeg_data);
            free(pixels);
            printf("Error decoding JPEG: %s\n", tjGetErrorStr());
            return NULL;
        }
        
        free(jpeg_data);*/
        
        //printf("Decoded %u %u jpeg. data size: %u\n", w, h, jpeg_size);
        // Set output parameters
        *width = w;
        *height = h;
        *return_size = jpeg_size;
        *timestamp = timestamp_read;
        
        return jpeg_data;
    }
    else {
        // Original non-chunked code path
        // Find Content-Type and Content-Length headers
        // char* content_type = NULL;
        int content_length = -1;
        char* headers_end = NULL;
        
        while (1) {
            if (stream->buffer_pos + 4 >= stream->buffer_len) {
                if (!fill_buffer(stream)) {
                    printf("cant decode frame: cant fill buffer(2)\n");
                    return NULL;
                }
            }
            
            headers_end = strstr(stream->buffer + stream->buffer_pos, "\r\n\r\n");
            if (headers_end) {
                *headers_end = '\0';  // Temporarily null-terminate for strstr
                
                // char* content_type_header = strstr(stream->buffer + stream->buffer_pos, "Content-Type: image/jpeg");
                char* content_length_header = strstr(stream->buffer + stream->buffer_pos, "Content-Length:");
                char* timestamp_header = strstr(stream->buffer + stream->buffer_pos, "X-Timestamp:");
                
                if (content_length_header) {
                    content_length = atoi(content_length_header + 16);
                }

                if(timestamp_header){
                    //printf("Found timestamp: ");
                    //printf(timestamp_header);
                    //printf("\n");
                    timestamp_read = atoll(timestamp_header + 13);
                }
                
                *headers_end = '\r';  // Restore the character
                headers_end += 4;     // Move past \r\n\r\n
                break;
            }
            
            if (!fill_buffer(stream)) {
                printf("cant decode frame: cant fill buffer(3)\n");
                return NULL;
            }
        }
        
        // Update buffer position
        stream->buffer_pos = headers_end - stream->buffer;
        
        // Rest of the original function for non-chunked encoding
        // [Original code for content_length > 0 and else blocks]
        // ...

        // Ensure we have the full JPEG data
        unsigned char* jpeg_data = NULL;
        if (content_length > 0) {
            // Known content length
            jpeg_data = (unsigned char*)malloc(content_length);
            if (!jpeg_data) {
                printf("cant decode frame: jpeg malloc null\n");
                return NULL;
            }
            
            // Copy what we already have
            size_t available = stream->buffer_len - stream->buffer_pos;
            size_t to_copy = (available > (unsigned)content_length) ? (unsigned)content_length : available;
            memcpy(jpeg_data, stream->buffer + stream->buffer_pos, to_copy);
            
            // Read the rest if needed
            size_t total_read = to_copy;
            stream->buffer_pos += to_copy;
            
            while (total_read < (unsigned)content_length) {
                if (stream->buffer_pos >= stream->buffer_len) {
                    if (!fill_buffer(stream)) {
                        free(jpeg_data);
                        printf("cant decode frame: cant fill buffer(4)\n");
                        return NULL;
                    }
                }
                
                available = stream->buffer_len - stream->buffer_pos;
                to_copy = (((unsigned)content_length - total_read) < available) ? 
                           ((unsigned)content_length - total_read) : available;
                
                memcpy(jpeg_data + total_read, stream->buffer + stream->buffer_pos, to_copy);
                total_read += to_copy;
                stream->buffer_pos += to_copy;
            }
        } else {
            // Unknown content length, read until next boundary
            // This is less efficient but should work
            size_t jpeg_buffer_size = 65536;  // Initial size
            size_t jpeg_size = 0;
            
            jpeg_data = (unsigned char*)malloc(jpeg_buffer_size);
            if (!jpeg_data) {
                printf("cant decode frame: cant malloc jpeg data(2)\n");
                return NULL;
            }
            
            while (1) {
                // Look for next boundary
                char* next_boundary = find_boundary(stream);
                if (next_boundary) {
                    // Found boundary, copy data up to boundary
                    size_t to_copy = next_boundary - (stream->buffer + stream->buffer_pos);
                    
                    if (jpeg_size + to_copy > jpeg_buffer_size) {
                        jpeg_buffer_size = jpeg_size + to_copy;
                        jpeg_data = (unsigned char*)realloc(jpeg_data, jpeg_buffer_size);
                        if (!jpeg_data) {
                            printf("cant decode frame: cant malloc jpeg data(3)\n");
                            return NULL;
                        }
                    }
                    
                    memcpy(jpeg_data + jpeg_size, stream->buffer + stream->buffer_pos, to_copy);
                    jpeg_size += to_copy;
                    stream->buffer_pos += to_copy;
                    break;
                }
                
                // No boundary found, copy everything and get more data
                size_t to_copy = stream->buffer_len - stream->buffer_pos;
                
                if (jpeg_size + to_copy > jpeg_buffer_size) {
                    jpeg_buffer_size = (jpeg_size + to_copy) * 2;
                    jpeg_data = (unsigned char*)realloc(jpeg_data, jpeg_buffer_size);
                    if (!jpeg_data) {
                        printf("cant decode frame: cant malloc jpeg data (4)\n");
                        return NULL;
                    }
                }
                
                memcpy(jpeg_data + jpeg_size, stream->buffer + stream->buffer_pos, to_copy);
                jpeg_size += to_copy;
                stream->buffer_pos = stream->buffer_len;
                
                if (!fill_buffer(stream)) {
                    free(jpeg_data);
                    printf("cant decode frame: cant fill buffer(7)\n");
                    return NULL;
                }
            }
            
            content_length = (int)jpeg_size;
        }
        
        // Decode JPEG data
        int jpegSubsamp, w, h;
        
        // Get the image dimensions first
        if (tjDecompressHeader2(stream->tjInstance, jpeg_data, content_length, &w, &h, &jpegSubsamp) < 0) {
            // Dump the bad JPEG data to a file
            //FILE* fp = fopen("./bad_data4.bin", "wb");
            //if (fp) {
            //    fwrite(jpeg_data, 1, content_length, fp);
            //    fclose(fp);
            //    printf("Dumped bad JPEG data to bad_data4.bin (%d bytes)\n", content_length);
            //}
            
            free(jpeg_data);
            return NULL;
        }
        
        /*// Allocate pixel buffer (RGBA format)
        int* pixels = (int*)malloc(w * h * sizeof(int));
        if (!pixels) {
            free(jpeg_data);
            printf("cant decode frame: cant allocate pixel buffer\n");
            return NULL;
        }
        
        // Decode the JPEG
        if (tjDecompress2(stream->tjInstance, jpeg_data, content_length, 
                         (unsigned char*)pixels, w, w * 4, h, TJPF_RGBA, 0) < 0) {
            free(pixels);
            free(pixels);
            printf("Error decoding JPEG: %s\n", tjGetErrorStr());
            return NULL;
        }
        
        free(jpeg_data);*/
        
        // Set output parameters
        *width = w;
        *height = h;
        *return_size = content_length;
        *timestamp = timestamp_read;
        
        return jpeg_data;
    }
}

// Close the stream and free resources
void CloseStream(MJPEGStream* stream) {
    if (stream) {
        if (stream->sock != SOCKET_INVALID) {
            CLOSE_SOCKET(stream->sock);
        }
        
        if (stream->tjInstance) {
            tjDestroy(stream->tjInstance);
        }
        
        free(stream);
    }
    
    cleanup_sockets();
}


/* usage:


#include <stdio.h>

int main() {
    // URL of the MJPEG stream
    const char* url = "http://example.com/mjpeg_stream";
    
    // Get stream handle
    MJPEGStream* stream = GetStreamHandle(url);
    if (!stream) {
        printf("Failed to connect to stream\n");
        return 1;
    }
    
    // Decode a few frames
    for (int i = 0; i < 10; i++) {
        int width, height;
        int* pixels = DecodeFrame(stream, &width, &height);
        
        if (pixels) {
            printf("Decoded frame %d: %d x %d pixels\n", i, width, height);
            
            // Do something with the pixel data...
            
            // Free the pixel data when done
            free(pixels);
        } else {
            printf("Failed to decode frame %d\n", i);
        }
    }
    
    // Close the stream
    CloseStream(stream);
    
    return 0;
}

*/