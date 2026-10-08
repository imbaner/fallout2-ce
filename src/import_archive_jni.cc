// The game's import on Android: ImportArchive for the app's Java side
// (os/android: com.alexbatalov.fallout2ce.NativeArchive). Exceptions of
// the Java callbacks (cancel, a failed write) stop the reading and go on to
// the Java caller.

#include <jni.h>

#include <string.h>

#include <algorithm>

#include "import_archive.h"
#include "save_composition.h"

using fallout::ImportArchive;
using fallout::ImportArchiveError;

namespace {

void throwFailure(JNIEnv* env, ImportArchiveError error, const std::string& message)
{
    if (env->ExceptionCheck()) {
        return;
    }
    jclass failureClass = env->FindClass("com/alexbatalov/fallout2ce/NativeArchive$Failure");
    if (failureClass == nullptr) {
        return;
    }
    jmethodID constructor = env->GetMethodID(failureClass, "<init>", "(ILjava/lang/String;)V");
    if (constructor == nullptr) {
        return;
    }
    jstring text = env->NewStringUTF(message.c_str());
    jobject failure = env->NewObject(failureClass, constructor, static_cast<jint>(error), text);
    if (failure != nullptr) {
        env->Throw(static_cast<jthrowable>(failure));
    }
}

ImportArchive* archiveOf(jlong handle)
{
    return reinterpret_cast<ImportArchive*>(static_cast<intptr_t>(handle));
}

// NewStringUTF wants modified UTF-8: names with characters past the BMP
// (4-byte UTF-8) go through UTF-16.
jstring newString(JNIEnv* env, const std::string& text)
{
    std::u16string utf16;
    size_t index = 0;
    while (index < text.size()) {
        uint8_t byte = static_cast<uint8_t>(text[index]);
        uint32_t code;
        size_t length;
        if (byte < 0x80) {
            code = byte;
            length = 1;
        } else if (byte < 0xE0) {
            code = byte & 0x1F;
            length = 2;
        } else if (byte < 0xF0) {
            code = byte & 0x0F;
            length = 3;
        } else {
            code = byte & 0x07;
            length = 4;
        }
        for (size_t next = 1; next < length && index + next < text.size(); next++) {
            code = (code << 6) | (static_cast<uint8_t>(text[index + next]) & 0x3F);
        }
        index += length;

        if (code >= 0x10000) {
            code -= 0x10000;
            utf16 += static_cast<char16_t>(0xD800 + (code >> 10));
            utf16 += static_cast<char16_t>(0xDC00 + (code & 0x3FF));
        } else {
            utf16 += static_cast<char16_t>(code);
        }
    }
    return env->NewString(reinterpret_cast<const jchar*>(utf16.data()), static_cast<jsize>(utf16.size()));
}

class JavaSink final : public fallout::ImportArchiveSink {
public:
    JavaSink(JNIEnv* env, jobject sink, jbyteArray buffer)
        : _env(env)
        , _sink(sink)
        , _buffer(buffer)
        , _bufferSize(static_cast<size_t>(env->GetArrayLength(buffer)))
    {
        jclass sinkClass = env->GetObjectClass(sink);
        _begin = env->GetMethodID(sinkClass, "begin", "(I)V");
        _data = env->GetMethodID(sinkClass, "data", "([BI)V");
        _end = env->GetMethodID(sinkClass, "end", "(I)V");
    }

    bool valid() const { return _begin != nullptr && _data != nullptr && _end != nullptr && _bufferSize > 0; }

    bool begin(size_t index) override
    {
        _env->CallVoidMethod(_sink, _begin, static_cast<jint>(index));
        return !_env->ExceptionCheck();
    }

    bool data(const uint8_t* bytes, size_t size) override
    {
        while (size > 0) {
            size_t chunk = std::min(size, _bufferSize);
            _env->SetByteArrayRegion(_buffer, 0, static_cast<jsize>(chunk), reinterpret_cast<const jbyte*>(bytes));
            _env->CallVoidMethod(_sink, _data, _buffer, static_cast<jint>(chunk));
            if (_env->ExceptionCheck()) {
                return false;
            }
            bytes += chunk;
            size -= chunk;
        }
        return true;
    }

    bool end(size_t index) override
    {
        _env->CallVoidMethod(_sink, _end, static_cast<jint>(index));
        return !_env->ExceptionCheck();
    }

private:
    JNIEnv* _env;
    jobject _sink;
    jbyteArray _buffer;
    size_t _bufferSize;
    jmethodID _begin;
    jmethodID _data;
    jmethodID _end;
};

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL Java_com_alexbatalov_fallout2ce_NativeArchive_nativeOpen(JNIEnv* env, jclass, jint fd, jobject progress)
{
    jmethodID update = nullptr;
    if (progress != nullptr) {
        update = env->GetMethodID(env->GetObjectClass(progress), "update", "(JJ)V");
        if (update == nullptr) {
            return 0;
        }
    }

    fallout::ImportArchiveProgress callback = [env, progress, update](uint64_t done, uint64_t total) {
        if (progress == nullptr) {
            return true;
        }
        env->CallVoidMethod(progress, update, static_cast<jlong>(done), static_cast<jlong>(total));
        return !env->ExceptionCheck();
    };

    ImportArchiveError error;
    std::string message;
    std::unique_ptr<ImportArchive> archive = ImportArchive::open(fd, callback, &error, &message);
    if (archive == nullptr) {
        // A cancel's exception is already pending.
        throwFailure(env, error, message);
        return 0;
    }
    return static_cast<jlong>(reinterpret_cast<intptr_t>(archive.release()));
}

JNIEXPORT jobjectArray JNICALL Java_com_alexbatalov_fallout2ce_NativeArchive_nativePaths(JNIEnv* env, jclass, jlong handle)
{
    const auto& entries = archiveOf(handle)->entries();
    jobjectArray paths = env->NewObjectArray(static_cast<jsize>(entries.size()), env->FindClass("java/lang/String"), nullptr);
    if (paths == nullptr) {
        return nullptr;
    }
    for (size_t index = 0; index < entries.size(); index++) {
        jstring path = newString(env, entries[index].path);
        if (path == nullptr) {
            return nullptr;
        }
        env->SetObjectArrayElement(paths, static_cast<jsize>(index), path);
        env->DeleteLocalRef(path);
    }
    return paths;
}

JNIEXPORT jlongArray JNICALL Java_com_alexbatalov_fallout2ce_NativeArchive_nativeSizes(JNIEnv* env, jclass, jlong handle)
{
    const auto& entries = archiveOf(handle)->entries();
    std::vector<jlong> values;
    values.reserve(entries.size());
    for (const auto& entry : entries) {
        values.push_back(static_cast<jlong>(entry.size));
    }
    jlongArray sizes = env->NewLongArray(static_cast<jsize>(values.size()));
    if (sizes != nullptr) {
        env->SetLongArrayRegion(sizes, 0, static_cast<jsize>(values.size()), values.data());
    }
    return sizes;
}

// Per entry: 1 - encrypted, 2 - its compression method isn't supported.
JNIEXPORT jintArray JNICALL Java_com_alexbatalov_fallout2ce_NativeArchive_nativeFlags(JNIEnv* env, jclass, jlong handle)
{
    const auto& entries = archiveOf(handle)->entries();
    std::vector<jint> values;
    values.reserve(entries.size());
    for (const auto& entry : entries) {
        values.push_back((entry.encrypted ? 1 : 0) | (entry.supported ? 0 : 2));
    }
    jintArray flags = env->NewIntArray(static_cast<jsize>(values.size()));
    if (flags != nullptr) {
        env->SetIntArrayRegion(flags, 0, static_cast<jsize>(values.size()), values.data());
    }
    return flags;
}

JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_NativeArchive_nativeRead(JNIEnv* env, jclass, jlong handle, jintArray indices, jbyteArray buffer, jobject sink)
{
    ImportArchive* archive = archiveOf(handle);
    jsize count = env->GetArrayLength(indices);
    std::vector<jint> values(static_cast<size_t>(count));
    env->GetIntArrayRegion(indices, 0, count, values.data());

    std::vector<size_t> wanted;
    for (jint value : values) {
        if (value < 0 || static_cast<size_t>(value) >= archive->entries().size()) {
            throwFailure(env, ImportArchiveError::kCorrupt, "no such entry");
            return;
        }
        wanted.push_back(static_cast<size_t>(value));
    }

    JavaSink javaSink(env, sink, buffer);
    if (!javaSink.valid()) {
        return;
    }
    if (!archive->read(wanted, javaSink)) {
        // The sink's exception, if it stopped the reading, goes on as is.
        throwFailure(env, archive->error(), archive->message());
    }
}

JNIEXPORT void JNICALL Java_com_alexbatalov_fallout2ce_NativeArchive_nativeClose(JNIEnv*, jclass, jlong handle)
{
    delete archiveOf(handle);
}

// A save made with [made] against the game made of [now]
// (save_composition.h): the level (SaveCompatibility), then each change as
// "kind<tab>name", one a line.
JNIEXPORT jstring JNICALL Java_com_alexbatalov_fallout2ce_SaveComposition_nativeCompare(JNIEnv* env, jclass, jstring made, jstring now)
{
    auto text = [env](jstring value) {
        std::string result;
        if (value != nullptr) {
            const char* chars = env->GetStringUTFChars(value, nullptr);
            if (chars != nullptr) {
                result = chars;
                env->ReleaseStringUTFChars(value, chars);
            }
        }
        return result;
    };

    std::vector<fallout::SaveCompatibilityChange> changes;
    fallout::SaveCompatibility level = fallout::saveCompositionCompare(text(made), text(now), &changes);
    std::string result = std::to_string(static_cast<int>(level));
    for (const auto& change : changes) {
        result += "\n" + std::to_string(static_cast<int>(change.kind)) + "\t" + change.name;
    }
    return env->NewStringUTF(result.c_str());
}

} // extern "C"
