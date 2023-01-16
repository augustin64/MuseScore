
#ifndef MAINAUDIO_H
#define MAINAUDIO_H

namespace MainAudio {

    void init();

    struct SynthRes {
        int done;  // bool
        float startTime; // the chunk's start time in seconds
        float endTime;   // the chunk's end time in seconds (playtime)
        unsigned chunkSize;
        char chunk[0 /* to be chunkSize */];
    };

    uintptr_t synthAudio(MainScore score, float starttime);
    const char* processSynth(uintptr_t fn_ptr, bool cancel);
    const char* processSynthBatch(uintptr_t fn_ptr, int batchSize, bool cancel);

} // namespace MainAudio

#endif // MAINAUDIO_H
