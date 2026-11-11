#pragma once
#include <chrono>
#include <cstdio>

// —татистика игрового тика: раз в 5 секунд печатает одну строку.
//   avg / max     - сколько работал game.tick()
//   late max      - насколько позже плана началс€ тик
//   over budget   - сколько тиков не уложились в бюджет (1000 / TICK_RATE мс)
//   skipped       - сколько раз сервер сильно отстал и пропустил врем€
// ¬ызываетс€ только из игрового потока.
class TickStats {
public:
    using clock = std::chrono::steady_clock;

    explicit TickStats(double budget_ms)
        : budget_ms_(budget_ms), last_report_(clock::now()) {}

    void add(double dur_ms, double late_ms)
    {
        ++count_;
        sum_ms_ += dur_ms;
        if (dur_ms > max_ms_) max_ms_ = dur_ms;
        if (late_ms > max_late_ms_) max_late_ms_ = late_ms;
        if (dur_ms > budget_ms_) ++overruns_;

        if (clock::now() - last_report_ >= std::chrono::seconds(5))
            report();
    }

    void skipped() { ++skipped_; }

private:
    void report()
    {
        std::printf("[tick] %d ticks: avg %.2f ms, max %.2f ms, late max %.2f ms, over budget %d, skipped %d\n",
            count_, count_ ? sum_ms_ / count_ : 0.0, max_ms_, max_late_ms_, overruns_, skipped_);
        std::fflush(stdout);

        count_ = overruns_ = skipped_ = 0;
        sum_ms_ = max_ms_ = max_late_ms_ = 0;
        last_report_ = clock::now();
    }

    double budget_ms_;
    clock::time_point last_report_;
    int count_ = 0, overruns_ = 0, skipped_ = 0;
    double sum_ms_ = 0, max_ms_ = 0, max_late_ms_ = 0;
};