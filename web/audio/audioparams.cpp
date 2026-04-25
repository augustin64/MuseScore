#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <type_traits>

#include "audio/worker/internal/workerplayback.h"

#include "./audioparams.h"
#include "./audiosynth.h"

using namespace muse;


static QJsonObject toQJsonObject(const audio::AudioOutputParams& params) {
    QJsonObject obj;
    obj.insert(QStringLiteral("volume"), params.volume.raw());
    obj.insert(QStringLiteral("balance"), params.balance);
    obj.insert(QStringLiteral("solo"), params.solo);
    obj.insert(QStringLiteral("muted"), params.muted);
    obj.insert(QStringLiteral("forceMute"), params.forceMute);
    return obj;
}

static Ret patchOutputParamsFromJson(const QJsonObject& patch, audio::AudioOutputParams& params) {
    const auto applyDouble = [&patch](QStringView key, auto& target) {
        if (!patch.contains(key))
            return make_ok();

        const QJsonValue value = patch.value(key);
        if (!value.isDouble()) {
            LOGE() << "Invalid type for '" << key.toString() << "': number expected";
            return make_ret(Ret::Code::InternalError);
        }
        target = static_cast<float>(value.toDouble());
        return make_ok();
    };

    const auto applyBool = [&patch](QStringView key, bool& target) {
        if (!patch.contains(key))
            return make_ok();

        const QJsonValue value = patch.value(key);
        if (!value.isBool()) {
            LOGE() << "Invalid type for '" << key.toString() << "': boolean expected";
            return make_ret(Ret::Code::InternalError);
        }
        target = value.toBool();
        return make_ok();
    };

    applyDouble(QStringLiteral("volume"), params.volume);
    applyDouble(QStringLiteral("balance"), params.balance);
    applyBool(QStringLiteral("solo"), params.solo);
    applyBool(QStringLiteral("muted"), params.muted);
    applyBool(QStringLiteral("forceMute"), params.forceMute);

    return make_ok();
}

namespace MainAudio {

Ret getOutputParams(MainScore score) {
    (void)score;

    const auto workerPlayback = modularity::globalIoc()->resolve<audio::worker::WorkerPlayback>("");

    const audio::TrackSequenceId sequenceId = Synth::getSequenceId();

    QJsonObject root;
    const auto masterParams = workerPlayback->masterOutputParams();
    if (!masterParams.ret) {
        return masterParams.ret;
    }
    root.insert(QStringLiteral("master"), toQJsonObject(masterParams.val));

    const auto trackIds = workerPlayback->trackIdList(sequenceId);
    if (!trackIds.ret) {
        return trackIds.ret;
    }
    
    QJsonArray tracks;
    for (const auto trackId : trackIds.val) {
        const auto trackName = QString::fromStdString(workerPlayback->trackName(sequenceId, trackId).val);
        const auto trackParams = workerPlayback->outputParams(sequenceId, trackId);
        if (!trackParams.ret) {
            return trackParams.ret;
        }

        QJsonObject trackObj = toQJsonObject(trackParams.val);
        trackObj.insert(QStringLiteral("trackId"), trackId);
        trackObj.insert(QStringLiteral("trackName"), trackName);
        tracks.append(trackObj);
    }
    root.insert(QStringLiteral("tracks"), tracks);

    Ret ret = make_ok();
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    ret.setData("wasm_payload", ByteArray(payload.constData(), payload.size()));
    return ret;
}

Ret setOutputParams(MainScore score, const char* json) {
    (void)score;

    const auto workerPlayback = modularity::globalIoc()->resolve<audio::worker::WorkerPlayback>("");

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return make_ret(Ret::Code::UnknownError, std::string("Invalid JSON payload"));
    }

    const QJsonObject payload = doc.object();

    const audio::TrackSequenceId sequenceId = Synth::getSequenceId();
    if (sequenceId == -1) {
        return make_ret(Ret::Code::UnknownError, std::string("No playback sequence found"));
    }

    if (payload.contains(QStringLiteral("master"))) {
        const QJsonValue masterValue = payload.value(QStringLiteral("master"));
        if (!masterValue.isObject()) {
            return make_ret(Ret::Code::UnknownError, std::string("Invalid type for 'master': object expected"));
        }

        const auto masterParamsRet = workerPlayback->masterOutputParams();
        if (!masterParamsRet.ret) {
            return masterParamsRet.ret;
        }

        auto masterParams = masterParamsRet.val;
        Ret ret = patchOutputParamsFromJson(masterValue.toObject(), masterParams);
        if (!ret) {
            return ret;
        }
        workerPlayback->setMasterOutputParams(masterParams);
    }

    if (payload.contains(QStringLiteral("tracks"))) {
        const QJsonValue tracksValue = payload.value(QStringLiteral("tracks"));
        if (!tracksValue.isArray()) {
            return make_ret(Ret::Code::UnknownError, std::string("Invalid type for 'tracks': array expected"));
        }

        const QJsonArray tracksArray = tracksValue.toArray();
        for (const QJsonValue& trackValue : tracksArray) {
            if (!trackValue.isObject()) {
                return make_ret(Ret::Code::UnknownError, std::string("Each track patch must be an object"));
            }

            const QJsonObject trackPatch = trackValue.toObject();
            const QJsonValue trackIdValue = trackPatch.value(QStringLiteral("trackId"));
            if (!trackIdValue.isDouble()) {
                return make_ret(Ret::Code::UnknownError, std::string("Track patch requires numeric 'trackId'"));
            }

            const audio::TrackId trackId = static_cast<audio::TrackId>(trackIdValue.toInt(-1));
            const auto trackParamsRet = workerPlayback->outputParams(sequenceId, trackId);
            if (!trackParamsRet.ret) {
                return trackParamsRet.ret;
            }

            auto params = trackParamsRet.val;
            Ret ret = patchOutputParamsFromJson(trackPatch, params);
            if (!ret) {
                return ret;
            }

            workerPlayback->setOutputParams(sequenceId, trackId, params);
        }
    }

    return make_ok();
}

} // namespace MainAudio