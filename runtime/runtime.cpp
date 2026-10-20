#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using I = std::int64_t;
struct Object {
    std::vector<I> values;
    std::vector<unsigned char> composite;
};
// Thread-local arenas own all allocations until that thread exits. Worker arenas
// are retained until main returns, so values escaping a callback remain alive.
struct Arena {
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<std::unique_ptr<std::string>> strings;
};
std::mutex arena_mutex;
std::vector<std::unique_ptr<Arena>> arenas;
Arena &arena() {
    thread_local Arena *local = [] {
        auto a = std::make_unique<Arena>();
        auto p = a.get();
        std::lock_guard<std::mutex> lock(arena_mutex);
        arenas.push_back(std::move(a));
        return p;
    }();
    return *local;
}
template <class T> I handle(T *p) {
    return reinterpret_cast<std::intptr_t>(p);
}
Object &object(I p) {
    return *reinterpret_cast<Object *>(static_cast<std::intptr_t>(p));
}
std::string &string(I p) {
    return *reinterpret_cast<std::string *>(static_cast<std::intptr_t>(p));
}
I make_string(std::string s) {
    auto p = std::make_unique<std::string>(std::move(s));
    I h = handle(p.get());
    arena().strings.push_back(std::move(p));
    return h;
}
I make_object(std::size_t n) {
    auto p = std::make_unique<Object>();
    p->values.resize(n);
    p->composite.resize(n);
    I h = handle(p.get());
    arena().objects.push_back(std::move(p));
    return h;
}
[[noreturn]] void error(const char *message) {
    throw std::runtime_error(message);
}
std::size_t size(I n) {
    if (n < 0)
        error("negative length");
    if (static_cast<std::uint64_t>(n) > std::numeric_limits<std::size_t>::max() / sizeof(I))
        error("length too large");
    return static_cast<std::size_t>(n);
}
void bounds(Object &o, I i) {
    if (i < 0 || static_cast<std::uint64_t>(i) >= o.values.size())
        error("array index out of bounds");
}
I clone(I h) {
    auto &source = object(h);
    I result = make_object(source.values.size());
    auto &dest = object(result);
    dest.composite = source.composite;
    for (std::size_t i = 0; i < source.values.size(); ++i)
        dest.values[i] = source.composite[i] ? clone(source.values[i]) : source.values[i];
    return result;
}
class Pool {
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> tasks;
    std::mutex mutex;
    std::condition_variable ready;
    bool stop = false;

  public:
    Pool() {
        std::size_t n = 1;
        if (auto raw = std::getenv("LAB_THREADS")) {
            std::string s = raw;
            unsigned long long parsed = 0;
            auto r = std::from_chars(s.data(), s.data() + s.size(), parsed);
            if (s.empty() || r.ec != std::errc{} || r.ptr != s.data() + s.size() || parsed == 0 ||
                parsed > 1024)
                error("LAB_THREADS must be an integer in 1..1024");
            n = static_cast<std::size_t>(parsed);
        }
        try {
            for (std::size_t i = 0; i < n; ++i)
                workers.emplace_back([this] {
                    for (;;) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(mutex);
                            ready.wait(lock, [&] { return stop || !tasks.empty(); });
                            if (stop && tasks.empty())
                                return;
                            task = std::move(tasks.front());
                            tasks.pop_front();
                        }
                        task();
                    }
                });
        } catch (...) {
            shutdown();
            throw;
        }
    }
    std::size_t count() const { return workers.size(); }
    void submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push_back(std::move(task));
        }
        ready.notify_one();
    }
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        ready.notify_all();
        for (auto &w : workers)
            if (w.joinable())
                w.join();
    }
    ~Pool() { shutdown(); }
};
Pool &pool() {
    static Pool p;
    return p;
}
} // namespace
extern "C" {
// All float values cross the ABI as their binary64 bit pattern, never as
// a pointer or a dynamically tagged value. Sema selects the correct operation.
I lab_float_bits(double value) {
    if (!std::isfinite(value))
        error("float overflow or non-finite value");
    I bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}
double lab_float_value(I bits) {
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
I lab_int_to_float(I value) {
    return lab_float_bits(static_cast<double>(value));
}
I lab_float_to_int(I bits) {
    double value = std::trunc(lab_float_value(bits));
    if (!std::isfinite(value) || value < -0x1p63 || value >= 0x1p63)
        error("float to int out of range");
    return static_cast<I>(value);
}
I lab_float_neg(I bits) {
    return lab_float_bits(-lab_float_value(bits));
}
I lab_float_op(I op, I left, I right) {
    double a = lab_float_value(left), b = lab_float_value(right);
    switch (op) {
    case 0:
        return lab_float_bits(a + b);
    case 1:
        return lab_float_bits(a - b);
    case 2:
        return lab_float_bits(a * b);
    case 3:
        if (b == 0)
            error("float division by zero");
        return lab_float_bits(a / b);
    case 4:
        return a == b;
    case 5:
        return a != b;
    case 6:
        return a < b;
    case 7:
        return a <= b;
    case 8:
        return a > b;
    case 9:
        return a >= b;
    default:
        error("invalid float operation");
    }
}
I lab_float_to_string(I bits) {
    char buffer[128];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), lab_float_value(bits),
                                std::chars_format::general);
    if (result.ec != std::errc{})
        error("cannot format float");
    return make_string(std::string(buffer, result.ptr));
}
I lab_string_to_float(I handle) {
    const auto &s = string(handle);
    double value = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (s.empty() || result.ec != std::errc{} || result.ptr != s.data() + s.size() ||
        !std::isfinite(value))
        error("invalid float string");
    return lab_float_bits(value);
}
I lab_int_neg(I a) {
    if (a == std::numeric_limits<I>::min())
        error("integer overflow");
    return -a;
}
I lab_int_op(I op, I a, I b) {
    I r = 0;
    if (op == 0) {
        if (__builtin_add_overflow(a, b, &r))
            error("integer overflow");
    } else if (op == 1) {
        if (__builtin_sub_overflow(a, b, &r))
            error("integer overflow");
    } else if (op == 2) {
        if (__builtin_mul_overflow(a, b, &r))
            error("integer overflow");
    } else {
        if (b == 0)
            error("division by zero");
        if (a == std::numeric_limits<I>::min() && b == -1)
            error("integer overflow");
        r = op == 3 ? a / b : a % b;
    }
    return r;
}
I lab_string(const char *data, I length) {
    return make_string(std::string(data, size(length)));
}
I lab_concat(I a, I b) {
    return make_string(string(a) + string(b));
}
I lab_equal(I a, I b) {
    return string(a) == string(b);
}
I lab_substr(I s, I begin, I end) {
    auto &v = string(s);
    if (begin < 0 || end < begin || static_cast<std::uint64_t>(end) > v.size())
        error("substring bounds");
    return make_string(v.substr(size(begin), size(end - begin)));
}
I lab_strlen(I s) {
    return static_cast<I>(string(s).size());
}
I lab_print(I s) {
    auto &v = string(s);
    if (std::fwrite(v.data(), 1, v.size(), stdout) != v.size() || std::fflush(stdout) != 0)
        error("output failure");
    return 0;
}
I lab_read_char() {
    int c = std::getchar();
    if (c == EOF) {
        if (std::ferror(stdin))
            error("input failure");
        return -1;
    }
    if (c > 127)
        error("non-ASCII input");
    return c;
}
I lab_to_string(I v, I is_bool) {
    return make_string(is_bool ? (v ? "true" : "false") : std::to_string(v));
}
I lab_to_int(I s) {
    auto &v = string(s);
    I n = 0;
    auto r = std::from_chars(v.data(), v.data() + v.size(), n);
    if (v.empty() || r.ec != std::errc{} || r.ptr != v.data() + v.size())
        error("invalid integer string");
    return n;
}
I lab_object(I n) {
    return make_object(size(n));
}
I lab_clone(I o) {
    return clone(o);
}
I lab_get(I o, I index) {
    auto &v = object(o);
    bounds(v, index);
    return v.values[size(index)];
}
I lab_set(I o, I index, I value, I composite) {
    auto &v = object(o);
    bounds(v, index);
    v.values[size(index)] = value;
    v.composite[size(index)] = composite != 0;
    return 0;
}
I lab_length(I o) {
    return static_cast<I>(object(o).values.size());
}
I lab_array(I n, I initial, I composite) {
    I h = make_object(size(n));
    auto &v = object(h);
    for (std::size_t i = 0; i < v.values.size(); ++i) {
        v.values[i] = composite ? clone(initial) : initial;
        v.composite[i] = composite != 0;
    }
    return h;
}
I lab_range(I start, I end, I step) {
    if (step == 0)
        error("range step must not be zero");
    auto p = std::make_unique<Object>();
    for (I i = start; step > 0 ? i < end : i > end;) {
        p->values.push_back(i);
        p->composite.push_back(0);
        I next;
        if (__builtin_add_overflow(i, step, &next))
            break;
        i = next;
    }
    I h = handle(p.get());
    arena().objects.push_back(std::move(p));
    return h;
}
I lab_parallel(I input, I environment, I (*callback)(I, I, I), I composite) {
    // Pool configuration is checked even for empty input.
    auto &executor = pool();
    auto &source = object(input);
    I result = make_object(source.values.size());
    auto &dest = object(result);
    std::fill(dest.composite.begin(), dest.composite.end(), composite != 0);
    std::atomic<std::size_t> next{0};
    std::mutex mutex;
    std::condition_variable finished;
    std::size_t remaining = 0;
    std::exception_ptr failure;
    std::size_t failed_index = source.values.size();
    auto task = [&] {
        for (;;) {
            auto i = next.fetch_add(1);
            if (i >= source.values.size())
                break;
            try {
                dest.values[i] = callback(static_cast<I>(i), source.values[i], environment);
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex);
                if (i < failed_index) {
                    failed_index = i;
                    failure = std::current_exception();
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            --remaining;
        }
        finished.notify_one();
    };
    // A submission failure must still wait for all successfully queued tasks.
    std::exception_ptr submission;
    for (std::size_t k = 0; k < std::min(executor.count(), source.values.size()); ++k) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++remaining;
        }
        try {
            executor.submit(task);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            --remaining;
            submission = std::current_exception();
            break;
        }
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        finished.wait(lock, [&] { return remaining == 0; });
    }
    if (submission)
        std::rethrow_exception(submission);
    if (failure)
        std::rethrow_exception(failure);
    return result;
}
I lab_entry();
}
int main() {
    try {
        I code = lab_entry();
        return static_cast<unsigned char>(code);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "runtime error: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "runtime error: unknown failure\n");
        return 1;
    }
}
