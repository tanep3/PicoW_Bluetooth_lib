#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// SDK-independent, single-main-loop only. No allocation, timers or request queue.
namespace sink_delay_sample {
constexpr uint32_t max_measured_us = 6553500;
constexpr size_t input_bytes_per_poll = 32;
constexpr size_t max_line_bytes = 64; // Excludes LF; includes optional final CR.

enum class Kind { None, Begin, Listen, Connect, Disconnect, Stop, Status, Report };
struct Command {
    Kind kind = Kind::None;
    uint32_t id = 0, generation = 0, output_epoch = 0, measured_us = 0;
};
enum class Parse { Pending, Complete, Malformed, TooLong };

class LineInput {
public:
    Parse push(unsigned char byte, Command &out) {
        // Reject the WHOLE line on overflow/binary/control input, drain to LF.
        // Never interpret a truncated prefix or the tail as another command.
        if (byte == '\n') {
            Parse result = error_;
            if (result == Parse::Pending) {
                size_t count = size_;
                if (count && bytes_[count - 1] == '\r') --count;
                result = decode(count, out) ? Parse::Complete : Parse::Malformed;
            }
            size_ = 0;
            error_ = Parse::Pending;
            return result;
        }
        if (error_ != Parse::Pending) return Parse::Pending;
        if (size_ == max_line_bytes) { error_ = Parse::TooLong; return Parse::Pending; }
        if ((byte < 32 && byte != '\r') || byte > 126 ||
            (size_ && bytes_[size_ - 1] == '\r')) {
            error_ = Parse::Malformed;
            return Parse::Pending;
        }
        bytes_[size_++] = static_cast<char>(byte);
        return Parse::Pending;
    }
private:
    std::array<char, max_line_bytes> bytes_{};
    size_t size_ = 0;
    Parse error_ = Parse::Pending;
    bool word(size_t size, const char *value) const {
        return size == std::strlen(value) && std::memcmp(bytes_.data(), value, size) == 0;
    }
    bool decode(size_t size, Command &out) const {
        out = {};
        if (word(size, "begin")) out.kind = Kind::Begin;
        else if (word(size, "listen")) out.kind = Kind::Listen;
        else if (word(size, "connect")) out.kind = Kind::Connect;
        else if (word(size, "disconnect")) out.kind = Kind::Disconnect;
        else if (word(size, "stop")) out.kind = Kind::Stop;
        else if (word(size, "status")) out.kind = Kind::Status;
        else {
            if (size < 7 || std::memcmp(bytes_.data(), "report ", 7)) return false;
            size_t at = 7;
            uint32_t fields[4]{};
            for (size_t field = 0; field < 4; ++field) {
                const size_t start = at;
                uint32_t n = 0;
                while (at < size && bytes_[at] >= '0' && bytes_[at] <= '9') {
                    const uint32_t digit = uint32_t(bytes_[at++] - '0');
                    if (n > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
                    n = n * 10 + digit;
                }
                if (at == start) return false;
                fields[field] = n;
                if (field < 3 && (at >= size || bytes_[at++] != ' ')) return false;
            }
            if (at != size) return false;
            out = {Kind::Report, fields[0], fields[1], fields[2], fields[3]};
        }
        return true;
    }
};

// Call reset() from the output reset callback. No API, logging or measurement
// occurs there. Saturation permanently invalidates tokens instead of wrapping.
class OutputBoundary {
public:
    explicit OutputBoundary(uint32_t initial = 1) : epoch_(initial), exhausted_(initial == 0) {}
    void reset() {
        if (epoch_ == std::numeric_limits<uint32_t>::max()) exhausted_ = true;
        else ++epoch_;
    }
    uint32_t epoch() const { return epoch_; }
    bool exhausted() const { return exhausted_; }
private:
    uint32_t epoch_;
    bool exhausted_;
};

struct Context {
    uint32_t generation = 0, output_epoch = 0;
    bool ready = false, epoch_exhausted = false;
};
// API-independent result names; the real adapter maps every public status.
enum class ReportStatus { Accepted, InvalidDelay, StaleSession, NotReady, NotNegotiated, SdkRejected };
struct ReportResult { ReportStatus status; uint8_t sdk_status = 0; };
enum class Outcome {
    None, Control, Malformed, TooLong, InvalidId, DuplicateOrOldId,
    InvalidDelay, BoundaryChanged, StaleMeasurement, NotReady, Stopped,
    Attempted, Reentry
};
struct Event {
    Outcome outcome = Outcome::None;
    Command command{};
    ReportResult result{ReportStatus::NotReady};
    uint32_t delay_100us = 0;
};

class Flow {
public:
    // reader() returns an unsigned byte, or -1 for no byte. Process <=32 bytes
    // and <=1 completed line per poll. A partial line owns the only input slot.
    // reporter(units, ORIGINAL_generation) is called synchronously at most once.
    template<class Reader, class Reporter>
    Event poll(Reader &&reader, const Context &current, Reporter &&reporter) {
        if (polling_) return {Outcome::Reentry};
        polling_ = true;
        struct Clear { bool &flag; ~Clear() { flag = false; } } clear{polling_};
        // Latch changes even if no input arrives while not ready, and readiness
        // returns before LF. End-of-line comparison alone would miss that gap.
        if (partial_ && differs(current)) boundary_changed_ = true;
        for (size_t i = 0; i < input_bytes_per_poll; ++i) {
            const int byte = reader();
            if (byte < 0) break;
            if (!partial_) { started_ = current; partial_ = true; boundary_changed_ = false; }
            Command command;
            const Parse parsed = input_.push(static_cast<unsigned char>(byte), command);
            if (parsed == Parse::Pending) continue;
            partial_ = false;
            if (parsed == Parse::Malformed) return {Outcome::Malformed};
            if (parsed == Parse::TooLong) return {Outcome::TooLong};
            if (command.kind != Kind::Report) return {Outcome::Control, command};
            return attempt(command, current, reporter);
        }
        return {};
    }
    void stop() { stopped_ = true; } // Terminal for this process; no reset/retry API.
    bool stopped() const { return stopped_; }
    uint32_t lastId() const { return last_id_; }
private:
    LineInput input_;
    Context started_{};
    bool partial_ = false, stopped_ = false, polling_ = false;
    uint32_t last_id_ = 0;
    bool boundary_changed_ = false;
    bool differs(const Context &current) const {
        return started_.generation != current.generation || started_.output_epoch != current.output_epoch ||
               started_.ready != current.ready || started_.epoch_exhausted != current.epoch_exhausted;
    }
    template<class Reporter>
    Event attempt(const Command &command, const Context &current, Reporter &reporter) {
        Event event{Outcome::InvalidId, command};
        if (!command.id) return event;
        if (command.id <= last_id_) { event.outcome = Outcome::DuplicateOrOldId; return event; }
        // Consume an ID even if rejected/busy: retransmitting it cannot send.
        // UINT32_MAX is valid once; there is deliberately no wrap/reset.
        last_id_ = command.id;
        if (stopped_) { event.outcome = Outcome::Stopped; return event; }
        if (command.measured_us > max_measured_us) { event.outcome = Outcome::InvalidDelay; return event; }
        if (boundary_changed_ || differs(current)) {
            event.outcome = Outcome::BoundaryChanged; return event;
        }
        if (!command.generation || !command.output_epoch || current.epoch_exhausted ||
            command.generation != current.generation || command.output_epoch != current.output_epoch) {
            event.outcome = Outcome::StaleMeasurement; return event;
        }
        if (!current.ready) { event.outcome = Outcome::NotReady; return event; }
        event.delay_100us = (command.measured_us + 99u) / 100u; // Bound checked first.
        event.result = reporter(event.delay_100us, command.generation);
        event.outcome = Outcome::Attempted;
        return event; // Never queue/retry on any API result.
    }
};
} // namespace sink_delay_sample
