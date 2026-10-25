// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cstring>

// Bounded UI-thread -> file-thread telemetry queue (SPSC).
// Audio callback NEVER interacts with this class. The UI samples atomics
// at 1 Hz and enqueues a compact record; file I/O occurs only on worker.
class DiagnosticsRecorder final : private juce::Thread
{
public:
    explicit DiagnosticsRecorder (const juce::File& directory)
      : juce::Thread ("AR MIX Diagnostic Writer"),
        folder_ (directory), logfile_ (directory.getChildFile ("audio-diagnostics.log"))
    {
        folder_.createDirectory();
        startThread();
        record ("SESSION", "started");
    }

    ~DiagnosticsRecorder() override
    {
        record ("SESSION", "closing");
        signalThreadShouldExit();
        notify();
        stopThread (5000);
    }

    // Producer: JUCE message thread only; may allocate, never RT.
    void record (const juce::String& kind, const juce::String& details)
    {
        const auto head = producer_.load (std::memory_order_relaxed);
        const auto next = (head + 1) % capacity;
        if (next == consumer_.load (std::memory_order_acquire))
        {
            lost_.fetch_add (1, std::memory_order_relaxed);
            return;
        }
        const auto timestamp = juce::Time::getCurrentTime().toISO8601 (true);
        juce::String message = timestamp + " | " + kind + " | " +
            details.replaceCharacters ("\r\n", "  ");
        // Message-thread-only recent history, independent of the file writer.
        // Retain glitch markers even if async disk writes have not completed.
        recent_[recentHead_] = message.length() > 950
            ? message.substring (0, 950) + " [report-truncated]" : message;
        recentHead_ = (recentHead_ + 1) % recentCapacity;
        recentCount_ = juce::jmin (recentCount_ + 1, recentCapacity);
        const auto utf8 = message.toRawUTF8();
        auto& slot = queue_[head];
        const auto inputLength = std::strlen (utf8);
        const auto count = juce::jmin (static_cast<size_t> (sizeof (slot.data) - 1),
                                       inputLength);
        if (inputLength > count)
            truncated_.fetch_add (1, std::memory_order_relaxed);
        std::memcpy (slot.data, utf8, count);
        slot.data[count] = 0;
        producer_.store (next, std::memory_order_release);
        notify();
    }

    // Fast UI-only report: tail is captured before enqueue and does not wait
    // for the writer thread or read a growing log file. Bounded to 64 records.
    juce::String recentReport (int maxRecords = 48) const
    {
        juce::String result;
        const auto count = juce::jmin (static_cast<unsigned> (juce::jlimit (0, 64, maxRecords)), recentCount_);
        const unsigned oldest = (recentHead_ + recentCapacity - count) % recentCapacity;
        for (unsigned n = 0; n < count; ++n)
        {
            result += recent_[(oldest + n) % recentCapacity];
            result += "\n";
        }
        return result;
    }

    juce::File getLogFile() const { return logfile_; }
    std::uint64_t lostRecords() const noexcept { return lost_.load(); }
    std::uint64_t truncatedRecords() const noexcept { return truncated_.load(); }

private:
    static constexpr unsigned recentCapacity = 64;
    std::array<juce::String, recentCapacity> recent_ {};
    unsigned recentHead_ = 0, recentCount_ = 0; // message-thread only
    static constexpr unsigned capacity = 128;
    struct Record { char data[4096] {}; };
    std::array<Record,capacity> queue_ {};
    std::atomic<unsigned> producer_ { 0 }, consumer_ { 0 };
    std::atomic<std::uint64_t> lost_ { 0 }, truncated_ { 0 };
    juce::File folder_, logfile_;

    void run() override
    {
        // One write per drain batch, never from the UI or audio callback.
        for (;;)
        {
            juce::String batch;
            unsigned tail = consumer_.load (std::memory_order_relaxed);
            const auto head = producer_.load (std::memory_order_acquire);
            while (tail != head)
            {
                batch += juce::String::fromUTF8 (queue_[tail].data);
                batch += "\n";
                tail = (tail + 1) % capacity;
            }
            consumer_.store (tail, std::memory_order_release);
            if (batch.isNotEmpty())
            {
                if (logfile_.getSize() > 2 * 1024 * 1024)
                {
                    auto rotated = folder_.getChildFile ("audio-diagnostics.previous.log");
                    rotated.deleteFile();
                    logfile_.moveFileTo (rotated);
                }
                logfile_.appendText (batch, false, false, "\n");
            }
            if (threadShouldExit() && tail == producer_.load (std::memory_order_acquire))
                break;
            wait (350);
        }
    }
};
