#pragma once
#include "A2dpSinkReceiver.h"

// Main-loop helper for an actual output pipeline, NOT the frame counter demo.
// Capture receiver.snapshot().generation when measuring this session's total
// receive-to-render delay. Include decoder + queued PCM + device delay as
// appropriate for your pipeline. Do not derive this from the FIFO size alone.
// Store the measurement with that generation; never retag it after reconnect.
// Call outside write()/reset()/poll(). This performs one attempt and no retry.
inline A2dpSinkReceiver::DelayReportResult reportMeasuredSinkLatency(
    A2dpSinkReceiver &receiver, uint32_t measured_latency_us,
    uint32_t measurement_generation) {
  // Round UP to 100 us without overflow; never clamp an out-of-range value.
  if (measured_latency_us > 6553500u)
    return {A2dpSinkReceiver::DelayReportStatus::InvalidDelay};
  return receiver.reportDelay100us((measured_latency_us + 99u) / 100u,
                                  measurement_generation);
}
