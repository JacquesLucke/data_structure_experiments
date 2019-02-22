#pragma once

#include <chrono>
#include <iostream>

class Timer {
  private:
    const char *name;
    std::chrono::high_resolution_clock::time_point start, end;
    std::chrono::duration<float> duration;

  public:
    Timer(const char *name = "") {
        this->name = name;
        this->start = std::chrono::high_resolution_clock::now();
    }

    ~Timer() {
        end = std::chrono::high_resolution_clock::now();
        duration = end - start;
        double ms = duration.count() * 1000.0f;
        std::cout << "Timer '" << name << "' took " << ms << " ms" << std::endl;
    }
};

#define TIMEIT(name) Timer t(name);