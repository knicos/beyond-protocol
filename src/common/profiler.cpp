#include <ftl/profiler.hpp>

#include <loguru.hpp>

#include <string>
#include <cstring>
#include <memory>
#include <vector>

//#include <absl/container/flat_hash_map.h>
#include <unordered_map>

#ifdef TRACY_ENABLE
void tracy_log_cb(void* user_data, const loguru::Message& message)
{
    size_t size = strlen(message.message);
    if (message.verbosity <= loguru::NamedVerbosity::Verbosity_ERROR)
    {
        TracyMessageC(message.message, size, tracy::Color::Red1);
    }
    else if (message.verbosity == loguru::NamedVerbosity::Verbosity_WARNING)
    {
        TracyMessageC(message.message, size, tracy::Color::Yellow1);
    }
    else
    {
        TracyMessageC(message.message, size, tracy::Color::WhiteSmoke);
    }
}
#endif
namespace ftl
{
// Enable/disable logging to profiler. Caller must synchronize (if necessary).
void profiler_logging_disable()
{
    #ifdef TRACY_ENABLE
    loguru::remove_callback("tracy_profiler");
    #endif
}
void profiler_logging_enable()
{
    #ifdef TRACY_ENABLE
    loguru::add_callback("tracy_profiler", tracy_log_cb, nullptr, loguru::NamedVerbosity::Verbosity_1);
    #endif
}
}

using map_t = std::unordered_map<std::string, const char*>;

inline bool MapContains(map_t map, const std::string& key)
{
    return map.find(key) != map.end();
}

/** Allocates blocks of memory and a flat_hash_map for lookups. */
struct PersistentStringBuffer
{
    const size_t BlockSize = 4096;
    size_t       Head      = 0;

    // unique_ptr shouldn't be used as memory is expected to be valid until exit.
    // However this code won't work with LeakSanitizer otherwise (as it considers it a leak).
    std::vector<std::unique_ptr<char[]>> Blocks;
    map_t HashMap;

    void AllocBlock()
    {
        Blocks.push_back(std::unique_ptr<char[]>(new char[BlockSize]));
        Head = 0;
    }

    PersistentStringBuffer()
    {
        AllocBlock();
    }

    ~PersistentStringBuffer()
    {

    }

    const char* GetOrInsert(const char* String)
    {
        if (MapContains(HashMap, String))
        {
            return HashMap[String];
        }

        auto* Ptr = Add(String);
        HashMap[String] = Ptr;
        return Ptr;
    }

    const char* Add(const char* String)
    {
        auto StringSize = std::strlen(String) + 1;
        CHECK(StringSize < BlockSize);

        if (StringSize > (BlockSize - Head))
        {
            AllocBlock();
        }

        char* PersistentString = Blocks.back().get() + Head;
        std::memcpy(PersistentString, String, StringSize);
        Head += StringSize;

        return PersistentString;
    }
};

#ifdef ENABLE_PROFILER

static PersistentStringBuffer PersistentStrings;
static std::mutex PersistentStringMutex;

const char* detail::GetPersistentString(const char* String)
{   
    std::unique_lock Lock(PersistentStringMutex);
    const auto* Str = PersistentStrings.GetOrInsert(String);
    CHECK(Str);
    return Str;
}

#else

const char* detail::GetPersistentString(const char* String) { return nullptr; }

#endif
