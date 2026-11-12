#include "FileTransport.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <optional>

FileTransport::FileTransport(const std::string& filename, Mode mode)
    : file_(nullptr), mode_(mode) {
    const char* modeStr = nullptr;
    switch (mode) {
        case Mode::WriteBinary: modeStr = "wb"; break;
        case Mode::ReadBinary:  modeStr = "rb"; break;
        case Mode::WriteText:   modeStr = "w";  break;
        case Mode::ReadText:    modeStr = "r";  break;
        default:                modeStr = "wb"; break;  // 万一の未定義に備える
    }
    file_ = fopen(filename.c_str(), modeStr);
    if (!file_) {
        printf("Failed to open file: %s\n", filename.c_str());
    }
}

FileTransport::~FileTransport() {
    if (file_) fclose(file_);
}

bool FileTransport::isReady() const {
    return file_ != nullptr;
}

bool FileTransport::send(const uint8_t* data, size_t size) {
    if (!file_ || (mode_ != Mode::WriteBinary && mode_ != Mode::WriteText)) return false;
    size_t written = fwrite(data, 1, size, file_);
    fflush(file_);
    return written == size;
}

std::vector<uint8_t> FileTransport::read(size_t size) {
    std::vector<uint8_t> buffer;
    if (!file_ || mode_ != Mode::ReadBinary) return buffer;
    buffer.resize(size);
    size_t bytesRead = fread(buffer.data(), 1, size, file_);
    buffer.resize(bytesRead); // 
    return buffer;
}

std::optional<std::string> FileTransport::readLine() {
    if (!file_ || mode_ != Mode::ReadText) return std::nullopt;
    char line[256];
    if (fgets(line, sizeof(line), file_)) {
        return std::string(line); 
    }
    return std::nullopt;
}
