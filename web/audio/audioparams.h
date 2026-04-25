
#ifndef MAINAUDIO_AUDIOPARAMS_H
#define MAINAUDIO_AUDIOPARAMS_H

#include "global/types/ret.h"
#include "./audiosynth.h"

namespace MainAudio {

muse::Ret getOutputParams(MainScore score);

muse::Ret setOutputParams(MainScore score, const char* json);

} // namespace MainAudio

#endif // MAINAUDIO_AUDIOPARAMS_H
