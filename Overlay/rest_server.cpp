#include "rest_server.h"
#include <iostream>
#include <cstring>
#include <sstream>

#ifdef _WIN32
    #pragma comment(lib, "ws2_32.lib")
    #define SOCKET_ERROR_VALUE INVALID_SOCKET
    #define close_socket closesocket
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #define SOCKET_ERROR_VALUE (-1)
    #define close_socket close
#endif

HTTPServer::HTTPServer(int port) : port_(port), running_(false) {
    #ifdef _WIN32
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    #endif
}

HTTPServer::~HTTPServer() {
    stop();
    #ifdef _WIN32
        WSACleanup();
    #endif
}

void HTTPServer::register_handler(const std::string& path, RequestHandler handler) {
    get_handlers_[path] = handler;
}

void HTTPServer::register_post_handler(const std::string& path, PostRequestHandler handler) {
    post_handlers_[path] = handler;
}

unsigned long ip_address_to_uint(const char* ip_address) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_address, &addr) != 1) {
        return INADDR_NONE;
    }
    return addr.s_addr;
}

void HTTPServer::start() {
    // Start the server in a new thread
    server_thread = std::thread([this]() {
        socket_t server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == SOCKET_ERROR_VALUE) {
            throw std::runtime_error("Failed to create socket");
        }

        struct sockaddr_in server_addr = {};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port_);

        int opt = 1;
        setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            std::string reason = strerror(errno);
            close_socket(server_socket);
            throw std::runtime_error("Failed to bind socket: " + reason);
        }

        if (listen(server_socket, 10) < 0) {
            close_socket(server_socket);
            throw std::runtime_error("Failed to listen on socket");
        }

        running_ = true;
        std::cout << "Server started on port " << port_ << std::endl;

        while (running_) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            socket_t client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

            if (client_socket == SOCKET_ERROR_VALUE) {
                if (!running_) break; // Exit cleanly if we're stopping the server
                continue;
            }

            handle_connection(client_socket);
            close_socket(client_socket);
        }

        close_socket(server_socket);
        std::cout << "Server stopped" << std::endl;
    });
}

void HTTPServer::stop() {
    if (running_) {
        running_ = false;
        
        // Create a connection to ourselves to unblock the accept() call
        socket_t temp_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (temp_socket != SOCKET_ERROR_VALUE) {
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            addr.sin_addr.s_addr = ip_address_to_uint("127.0.0.1");
            
            connect(temp_socket, (struct sockaddr*)&addr, sizeof(addr));
            close_socket(temp_socket);
        }
        
        // Wait for the server thread to finish
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
}

void HTTPServer::parse_request(const std::string& request, 
                             std::string& method, 
                             std::string& path, 
                             std::unordered_map<std::string, std::string>& headers,
                             std::unordered_map<std::string, std::string>& params,
                             std::string& body) {
    std::istringstream request_stream(request);
    std::string line;
    
    // Parse request line
    std::getline(request_stream, line);
    std::istringstream request_line(line);
    std::string version;
    request_line >> method >> path >> version;

    // Parse query parameters if present
    size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        std::string query = path.substr(query_pos + 1);
        path = path.substr(0, query_pos);
        
        std::istringstream query_stream(query);
        std::string param;
        while (std::getline(query_stream, param, '&')) {
            size_t eq_pos = param.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = param.substr(0, eq_pos);
                std::string value = param.substr(eq_pos + 1);
                params[key] = value;
            }
        }
    }

    // Parse headers
    while (std::getline(request_stream, line) && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            // Skip the colon and any leading whitespace
            size_t value_start = line.find_first_not_of(" \t", colon_pos + 1);
            if (value_start != std::string::npos) {
                std::string value = line.substr(value_start);
                // Remove trailing \r if present
                if (!value.empty() && value.back() == '\r') {
                    value.pop_back();
                }
                headers[key] = value;
            }
        }
    }

    // Extract body if present
    if (method == "POST" || method == "PUT") {
        // Find the content length header
        auto content_length_it = headers.find("Content-Length");
        if (content_length_it != headers.end()) {
            int content_length = std::stoi(content_length_it->second);
            
            // The body starts after the empty line (headers and body separator)
            size_t body_start = request.find("\r\n\r\n");
            if (body_start != std::string::npos) {
                body_start += 4; // Skip the \r\n\r\n
                body = request.substr(body_start, content_length);
            }
        }
    }
}

void HTTPServer::handle_connection(socket_t client_socket) {
    char buffer[8192] = {0}; // Increased buffer size for larger requests
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_read <= 0) {
        return;
    }

    // Parse HTTP request
    std::string request(buffer);
    std::string method, path, body;
    std::unordered_map<std::string, std::string> headers;
    std::unordered_map<std::string, std::string> params;
    
    parse_request(request, method, path, headers, params, body);

    // Find and call the appropriate handler
    std::string response_body;
    
    if (method == "GET") {
        auto it = get_handlers_.find(path);
        if (it != get_handlers_.end()) {
            response_body = it->second(params);
        } else {
            response_body = "{\"ERROR\": \"Path not found for GET request!\"}";
        }
    } else if (method == "POST") {
        auto it = post_handlers_.find(path);
        if (it != post_handlers_.end()) {
            response_body = it->second(params, body);
        } else {
            response_body = "{\"ERROR\": \"Path not found for POST request!\"}";
        }
    } else {
        response_body = "{\"ERROR\": \"Method not supported!\"}";
    }

    // Send response
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << response_body.length() << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << response_body;

    std::string response_str = response.str();
    send(client_socket, response_str.c_str(), (int)response_str.length(), 0);
}