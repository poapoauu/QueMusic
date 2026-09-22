#pragma once

class RuntimeLoggingPolicy final {
public:
    RuntimeLoggingPolicy() = delete;

    // Must be called before QGuiApplication construction so backend debug
    // categories cannot print authenticated media descriptors during startup.
    static void install();
};
