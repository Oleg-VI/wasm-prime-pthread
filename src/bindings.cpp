#include <emscripten/bind.h>
#include <emscripten/threading.h>
#include <emscripten/val.h>

#include "prime_generator.h"

using namespace emscripten;

class PrimeGeneratorAdapter {
public:
    PrimeGeneratorAdapter()
        : on_progress_(val::undefined())
        , on_complete_(val::undefined())
    {}

    bool StartComputation(int limit, val on_progress, val on_complete) {
        // Store val callbacks here on the main thread — never touch them
        // from the worker thread.
        on_progress_ = on_progress;
        on_complete_ = on_complete;

        return generator_.StartComputation(
            limit,
            // Worker thread captures only `this` (a plain pointer — safe).
            [this](int percent) {
                auto* args = new ProgressArgs{this, percent};
                emscripten_async_run_in_main_runtime_thread(
                    EM_FUNC_SIG_VI,
                    reinterpret_cast<void*>(&dispatch_progress),
                    static_cast<int>(reinterpret_cast<intptr_t>(args)));
            },
            [this](std::vector<int> primes) {
                auto* args = new CompleteArgs{this, std::move(primes)};
                emscripten_async_run_in_main_runtime_thread(
                    EM_FUNC_SIG_VI,
                    reinterpret_cast<void*>(&dispatch_complete),
                    static_cast<int>(reinterpret_cast<intptr_t>(args)));
            }
        );
    }

    void Cancel()          { generator_.Cancel();    }
    bool IsRunning() const { return generator_.IsRunning(); }

private:
    // Callbacks stored on the main thread — val is never crossed to a worker.
    val on_progress_;
    val on_complete_;
    PrimeGenerator generator_;

    struct ProgressArgs {
        PrimeGeneratorAdapter* self;
        int percent;
    };

    struct CompleteArgs {
        PrimeGeneratorAdapter* self;
        std::vector<int> primes;
    };

    // These always run on the main thread — safe to call JS from here.
    static void dispatch_progress(int raw) noexcept {
        auto* a = reinterpret_cast<ProgressArgs*>(raw);
        a->self->on_progress_(a->percent);
        delete a;
    }

    static void dispatch_complete(int raw) noexcept {
        auto* a = reinterpret_cast<CompleteArgs*>(raw);

        // typed_memory_view gives JS a zero-copy view into WASM memory.
        // We then copy into a standalone Int32Array so the vector can be freed.
        val view = val(typed_memory_view(
            a->primes.size(),
            a->primes.data()));
        val int32array = val::global("Int32Array").new_(view);

        a->self->on_complete_(int32array);
        delete a;
    }
};

EMSCRIPTEN_BINDINGS(prime_wasm) {
    class_<PrimeGeneratorAdapter>("PrimeGenerator")
        .constructor<>()
        .function("StartComputation", &PrimeGeneratorAdapter::StartComputation)
        .function("Cancel",           &PrimeGeneratorAdapter::Cancel)
        .function("IsRunning",        &PrimeGeneratorAdapter::IsRunning);
}
