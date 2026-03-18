
#include "global/log.h"
#include "async/processevents.h"

#include "context/iglobalcontext.h"

#include "audio/main/iplayback.h"
#include "audio/worker/iaudioengine.h"

#include "notation/imasternotation.h"

#include "playback/iplaybackcontroller.h"

#include "./audiosynth.h"

using namespace muse;

namespace MainAudio {

std::vector<std::function<Synth::SynthRes*(bool)>> Synth::synthIterators;

namespace {
constexpr char AUDIO_MODULE[] = "audio";
constexpr char AUDIO_WORKER_MODULE[] = "audio_worker";
constexpr int MAX_ASYNC_PUMPS = 64;

void pumpAsync(int iterations = 1)
{
    for (int index = 0; index < iterations; ++index) {
        async::processEvents();
    }
}

mu::notation::INotationPtr resolveNotationForScore(MainScore score)
{
    auto globalContext = modularity::globalIoc()->resolve<mu::context::IGlobalContext>("");
    if (!globalContext) {
        return nullptr;
    }

    auto masterNotation = globalContext->currentMasterNotation();
    if (!masterNotation) {
        return nullptr;
    }

    auto* targetScore = reinterpret_cast<mu::engraving::Score*>(static_cast<mu::engraving::MasterScore*>(score));

    auto master = masterNotation->notation();
    if (master && master->elements() && master->elements()->msScore() == targetScore) {
        return master;
    }

    for (const auto& excerpt : masterNotation->excerpts()) {
        if (!excerpt) {
            continue;
        }

        auto notation = excerpt->notation();
        if (notation && notation->elements() && notation->elements()->msScore() == targetScore) {
            return notation;
        }
    }

    return nullptr;
}

audio::TrackSequenceId resolveSequenceId(const std::shared_ptr<mu::playback::IPlaybackController>& playbackController,
                                        const std::shared_ptr<audio::IPlayback>& playback)
{
    for (int attempt = 0; attempt < MAX_ASYNC_PUMPS; ++attempt) {
        audio::TrackSequenceId sequenceId = playbackController->currentTrackSequenceId();
        if (sequenceId != -1) {
            return sequenceId;
        }

        pumpAsync();
    }

    audio::TrackSequenceIdList sequenceIds;
    bool done = false;

    playback->sequenceIdList()
        .onResolve(nullptr, [&sequenceIds, &done](const audio::TrackSequenceIdList& ids) {
            sequenceIds = ids;
            done = true;
        })
        .onReject(nullptr, [&done](int, const std::string&) {
            done = true;
        });

    for (int attempt = 0; attempt < MAX_ASYNC_PUMPS && !done; ++attempt) {
        pumpAsync();
    }

    return sequenceIds.empty() ? -1 : sequenceIds.front();
}
}

/**
 * De-interleave audio channels
 * @param dest: [ channelA #len frames, channelB #len frames ]
 * @param src:  [ channelA frame0, channelB frame0, channelA frame1, channelB frame1, ... ]
 */
void deInterleave(float* dest, const float* src, size_t framesLen) {
    for (size_t i = 0, j = 0; i < framesLen; i++, j+=2) {
        dest[i] = src[j];
        dest[framesLen + i] = src[j+1];
    }
}

const char* Synth::processBatch(int batchSize, bool cancel) {
    auto resArr = (SynthRes**)calloc(batchSize, sizeof(SynthRes*)); // array of pointers to SynthRes data 
    for (int i = 0; i < batchSize; i++) {
        resArr[i] = (*synthFn)(cancel);
    }
    return reinterpret_cast<const char*>(resArr);
}

Synth Synth::start(MainScore score, float starttime) {
    LOGI() << String(u"starttime %1").arg(starttime);

    auto globalContext = modularity::globalIoc()->resolve<mu::context::IGlobalContext>("");
    auto playbackController = modularity::globalIoc()->resolve<mu::playback::IPlaybackController>("");
    auto playback = modularity::globalIoc()->resolve<audio::IPlayback>(AUDIO_MODULE);
    auto audioEngine = modularity::globalIoc()->resolve<audio::worker::IAudioEngine>(AUDIO_WORKER_MODULE);

    IF_ASSERT_FAILED(globalContext && playbackController && playback && audioEngine) {
        LOGE() << "audio services are not available";
        return nullptr;
    }

    if (auto notation = resolveNotationForScore(score)) {
        globalContext->setCurrentNotation(notation);
        playbackController->setNotation(notation);
    }

    // use buffer size of 512 frames
    static const size_t renderStep = 512;
    static const size_t channels = 2;
    static const size_t sampleRate = 44100;

    pumpAsync(2);

    const audio::TrackSequenceId sequenceId = resolveSequenceId(playbackController, playback);
    IF_ASSERT_FAILED(sequenceId != -1) {
        LOGE() << "no playback sequence found";
        return nullptr;
    }

    auto player = playback->player(sequenceId);
    IF_ASSERT_FAILED(player) {
        LOGE() << "no playback player found for sequence";
        return nullptr;
    }

    int totalDurationMsec = playbackController->totalPlayTime().msecsSinceStartOfDay();
    for (int attempt = 0; attempt < MAX_ASYNC_PUMPS && totalDurationMsec <= 0; ++attempt) {
        pumpAsync();
        totalDurationMsec = playbackController->totalPlayTime().msecsSinceStartOfDay();
    }

    IF_ASSERT_FAILED(totalDurationMsec > 0) {
        LOGE() << "playback duration is empty";
        return nullptr;
    }

    player->setDuration(totalDurationMsec);

    // Seek
    // https://github.com/LibreScore/webmscore/blob/v4.0/src/framework/audio/internal/worker/audiooutputhandler.cpp#L200-L201
    player->stop();
    player->seek(starttime);

    // Setup audio source
    // https://github.com/LibreScore/webmscore/blob/v4.0/src/framework/audio/internal/soundtracks/soundtrackwriter.cpp#L73-L76
    const auto previousMode = audioEngine->mode();
    const auto previousSampleRate = audioEngine->sampleRate();
    audioEngine->setMode(audio::RenderMode::OfflineMode);
    auto source = audioEngine->mixer();
    IF_ASSERT_FAILED(source) {
        audioEngine->setMode(previousMode);
        LOGE() << "no audio mixer available";
        return nullptr;
    }
    source->setSampleRate(sampleRate);
    source->setIsActive(true);

    // https://github.com/LibreScore/webmscore/blob/v4.0/src/framework/audio/internal/soundtracks/soundtrackwriter.cpp#L49
    const audio::samples_t totalSamples = (float(totalDurationMsec) / 1000.f) * sampleRate;
    LOGI() << String(u"totalDuration %1, totalSamples %2").arg(totalDurationMsec).arg((int64_t)totalSamples);

    bool done = false;
    bool cleanedUp = false;
    audio::samples_t playedSamples = starttime * sampleRate;
    auto synthIterator = [done, cleanedUp, playedSamples, totalSamples, source, audioEngine, previousMode,
                          previousSampleRate](bool cancel = false) mutable -> SynthRes* {
        auto cleanup = [&]() {
            if (cleanedUp) {
                return;
            }

            source->setIsActive(false);
            source->setSampleRate(previousSampleRate);
            audioEngine->setMode(previousMode);
            cleanedUp = true;
        };

        if (done) {
            cleanup();
            return new SynthRes{done, -1, -1, 0, {}};
        }

        float buffer[renderStep * channels] = {};
        auto res = (SynthRes*)calloc(1, sizeof(SynthRes) + sizeof(buffer)); 
        res->chunkSize = sizeof(buffer);

        // render audio buffer
        source->process(buffer, renderStep);
        deInterleave((float*)res->chunk, buffer, renderStep);

        auto prevPlayed = playedSamples;
        playedSamples += renderStep;
        if (playedSamples >= totalSamples || cancel) {
            done = true;
            cleanup();
        }

        res->done = done;
        res->startTime = float(prevPlayed) / sampleRate;
        res->endTime = float(playedSamples) / sampleRate;

        return res;
    };

    // persist this `synthIterator` function
    synthIterators.push_back(synthIterator);

    return Synth(&synthIterators.back());
}

} // namespace MainAudio
