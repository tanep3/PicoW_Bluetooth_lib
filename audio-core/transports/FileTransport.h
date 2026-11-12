#pragma once
#include "ITransport.h"
#include <stdio.h>
#include <string>
#include <optional>
#include <vector>

class FileTransport : public ITransport {
public:
    enum class Mode {
        WriteBinary,
        WriteText,
        ReadBinary,
        ReadText
    };

    FileTransport(const std::string& filename, Mode mode);
    ~FileTransport();

    bool isReady() const override;
    bool send(const uint8_t* data, size_t size) override;
    std::vector<uint8_t> read(size_t size);
    std::optional<std::string> readLine();

private:
    FILE* file_;
    Mode mode_;
};
