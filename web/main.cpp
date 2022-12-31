
#include <emscripten/emscripten.h>

#include <QGuiApplication>
#include <QByteArray>
#include <QBuffer>
#include <QTemporaryFile>
#include <QDate>
#include <QJsonObject>
#include <QJsonDocument>

#include "libmscore/excerpt.h"
#include "libmscore/part.h"
#include "libmscore/importexports.h"
#include "libmscore/mscore.h"
#include "libmscore/score.h"
#include "libmscore/text.h"
#include "libmscore/undo.h"
#include "mscore/preferences.h"

/**
 * helper functions
 */

/**
 * realloc/copy the data block so that it can be properly referred by its ptr and then freed in c-style
 */
const char* reallocData(QByteArray data) {
    auto size = data.size() + 1; // https://doc.qt.io/qt-5/qbytearray.html#data
    auto buf = (char*)malloc(size);
    memcpy(buf, data.constData(), size);
    return buf;
}

/**
 * pack length-prefixed data
 */
const char* packData(QByteArray data, qint64 size) {
    QByteArray sizeData = QByteArray((const char*)&size, 4);

    QBuffer result;
    result.open(QIODevice::ReadWrite);
    result.write(sizeData);
    result.write(data);
    result.close();

    return reallocData(result.data());
}

Ms::Score* maybeUseExcerpt(Ms::Score* score, int excerptId) {
    // -1 means the full score
    if (excerptId >= 0) {
        QList<Ms::Excerpt*> excerpts = score->excerpts();

        if (excerptId >= excerpts.size()) {
            throw(QString("Not a valid excerptId."));
        }

        score = excerpts[excerptId]->partScore();
        qDebug("useExcerpt: %d", excerptId);
    }

    return score;
}

/**
 * the MSCZ/MSCX file format version
 */
int _version() {
    return Ms::MSCVERSION;
}

/**
 * init libmscore
 */
void _init(int argc, char** argv) {
    setenv("QT_QPA_PLATFORM", "offscreen", 1); // https://stackoverflow.com/a/70978934
    setenv("QT_QPA_FONTDIR", "/fonts", 1);
    new QGuiApplication(argc, argv);

    Ms::preferences.init();

    Ms::MScore::noGui = true;
    Ms::MScore::debugMode = false;
    Ms::MScore::init();
}

/**
 * load (CJK) fonts on demand
 */
bool _addFont(const char* fontPath) {
    QString _fontPath = QString::fromUtf8(fontPath);

    if (-1 == QFontDatabase::addApplicationFont(_fontPath)) {
        qDebug("Cannot load font <%s>", qPrintable(_fontPath));
        return false;
    } else {
        return true;
    }
}

/**
 * load the score data (a MSCZ/MSCX file buffer)
 */
uintptr_t _load(const char* format, const char* data, const uint32_t size, bool doLayout) {
    using namespace Ms;

    QString _format = QString::fromUtf8(format);  // file format of the data

    MasterScore* score = new MasterScore(MScore::baseStyle());
    score->setMovements(new Movements());

    // create a temporary file, and write `data` into it
    QTemporaryFile tempfile("XXXXXX." + _format);  // filename template for the temporary file
    if (!tempfile.open()) { // a QTemporaryFile will always be opened in `QIODevice::ReadWrite` mode
        throw QString("Cannot create a temporary file");
    } else {
        tempfile.write(data, size);
        tempfile.close(); // calls QFileDevice::flush() and closes the file
    }
    QString name = tempfile.fileName(); // temporary filename

    // mtest/testutils.cpp#L108-L134 readCreatedScore
    // mscore/file.cpp#L2320 readScore
    Score::FileError rv;
    if (_format == "mscz" || _format == "mscx")
        rv = score->loadMsc(name, true);
    else if (_format == "mxl")
        rv = importCompressedMusicXml(score, name);
    else if (_format == "xml" || _format == "musicxml")
        rv = importMusicXml(score, name);
    else if (_format == "midi" || _format == "kar")
        rv = importMidi(score, name);
    else if (_format == "gtp" || _format == "gp3" || _format == "gp4" || _format == "gp5" || _format == "gpx" || _format == "gp" || _format == "ptb")
        rv = importGTP(score, name);
    else {
        qWarning("Invalid file format");
        rv = Score::FileError::FILE_UNKNOWN_TYPE;
    }

    // delete the temporary file
    tempfile.remove();

    // handle exceptions
    if (rv != Score::FileError::FILE_NO_ERROR) {
        return char(rv);
    }

    // post processing for non-native formats
    if (!(_format == "mscz" || _format == "mscx")) {
        score->setMetaTag("originalFormat", _format);
        score->connectTies();
    }

    // mscore/file.cpp#L2387 readScore
    score->rebuildMidiMapping();
    score->setSoloMute();
    for (auto s : score->scoreList()) {
        s->setPlaylistDirty();
        s->addLayoutFlags(LayoutFlag::FIX_PITCH_VELO);
        s->setLayoutAll();
    }
    score->updateChannel();
    // score->updateExpressive(MuseScore::synthesizer("Fluid"));

    if (doLayout) {
        // do layout ...
        score->update();
        score->switchToPageMode();  // the default _layoutMode is LayoutMode::PAGE, but the score file may be saved in continuous mode
    }

    return reinterpret_cast<uintptr_t>(score);
}

/**
 * Generate excerpts from Parts (only parts that are visible) if no existing excerpts
 */
void _generateExcerpts(uintptr_t score_ptr) {
    auto score = reinterpret_cast<Ms::MasterScore*>(score_ptr);

    QList<Ms::Excerpt*> scoreExcerpts = score->excerpts();
    if (scoreExcerpts.size() > 0) {
        // has existing excerpts
        return;
    }

    auto excerpts = Ms::Excerpt::createAllExcerpt(score);

    for (auto e : excerpts) {
        auto nscore = new Ms::Score(e->oscore());
        e->setPartScore(nscore);
        nscore->style().set(Ms::Sid::createMultiMeasureRests, true);
        auto excerptCmdFake = new Ms::AddExcerpt(e);
        excerptCmdFake->redo(nullptr);
        Ms::Excerpt::createExcerpt(e);

        // add this excerpt back to the score excerpt list
        scoreExcerpts.append(e);
    }

    qDebug("Generated excerpts: size %d", excerpts.size());
}

/**
 * get the score title
 */
const char* _title(uintptr_t score_ptr) {
    Ms::MasterScore* score = reinterpret_cast<Ms::MasterScore*>(score_ptr);

    // code from MuseScore::saveMetadataJSON
    QString title;
    Ms::Text* t = score->getText(Ms::Tid::TITLE);
    if (t)
        title = t->plainText();
    if (title.isEmpty())
        title = score->metaTag("workTitle");
    if (title.isEmpty())
        title = score->title();

    return reallocData(
        title.toUtf8()
    );
}

/**
 * get the number of pages
 */
int _npages(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);
    return score->npages();
}

/**
 * export score as MusicXML file
 */
const char* _saveXml(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);

    Ms::saveXml(score, &buffer);
    qDebug("saveXml: excerpt %d, size %lld bytes", excerptId, buffer.size());

    // MusicXML is plain text
    return reallocData(
        QString(buffer.data()).toUtf8()
    );
}

/**
 * export score as compressed MusicXML file
 */
const char* _saveMxl(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);

    // compressed MusicXML
    Ms::saveMxl(score, &buffer);

    auto size = buffer.size();
    qDebug("saveMxl: excerpt %d, size %lld", excerptId, size);

    return packData(buffer.data(), size);
}

/**
 * save part score as MSCZ/MSCX file
 */
const char* _saveMsc(uintptr_t score_ptr, bool compressed, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    if (!score->isMaster()) {  // clone metaTags from masterScore
        QMapIterator<QString, QString> j(score->masterScore()->metaTags());
        while (j.hasNext()) {
            j.next();
            if (j.key() != "partName")  // don't copy "partName" should that exist in masterScore
                score->metaTags().insert(j.key(), j.value());
            score->metaTags().insert("platform", "webmscore");
            score->metaTags().insert("source", "https://github.com/LibreScore/webmscore");
            score->metaTags().insert("creationDate", QDate::currentDate().toString(Qt::ISODate));  // update "creationDate"
        }
    }

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);

    if (compressed) {
        score->saveCompressedFile(&buffer, "score.mscx", false, true);
    } else {
        score->saveFile(&buffer, false, false);
    }

    if (!score->isMaster()) {  // remove metaTags added above
        QMapIterator<QString, QString> j(score->masterScore()->metaTags());
        while (j.hasNext()) {
            j.next();
            // remove all but "partName", should that exist in masterScore
            if (j.key() != "partName")
                score->metaTags().remove(j.key());
        }
    }

    auto size = buffer.size();
    qDebug("saveMsc: compressed %d, excerpt %d, size %lld", compressed, excerptId, size);

    return packData(buffer.data(), size);
}

/**
 * export score as SVG
 */
const char* _saveSvg(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);

    score->switchToPageMode();
    Ms::saveSvg(score, &buffer, pageNumber, drawPageBackground);
    qDebug("saveSvg: excerpt %d, page index %d, size %lld bytes", excerptId, pageNumber, buffer.size());

    // SVG is plain text
    return reallocData(
        QString(buffer.data()).toUtf8()
    );
}

/**
 * export score as PNG
 */
const char* _savePng(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, bool transparent, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);

    score->switchToPageMode();
    Ms::savePng(score, &buffer, pageNumber, drawPageBackground, transparent);

    auto size = buffer.size();
    qDebug("savePng: excerpt %d, page index %d, drawPageBackground %d, transparent %d, size %lld bytes", excerptId, pageNumber, drawPageBackground, transparent, size);

    return packData(buffer.data(), size);
}

/**
 * export score as PDF
 */
const char* _savePdf(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);

    Ms::savePdf(score, &buffer);

    auto size = buffer.size();
    qDebug("savePdf: excerpt %d, size %lld", excerptId, size);

    return packData(buffer.data(), size);
}

/**
 * export score as MIDI
 */
const char* _saveMidi(uintptr_t score_ptr, bool midiExpandRepeats, bool exportRPNs, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);

    Ms::saveMidi(score, &buffer, midiExpandRepeats, exportRPNs);

    auto size = buffer.size();
    qDebug("saveMidi: excerpt %d, midiExpandRepeats %d, exportRPNs %d, size %lld", excerptId, midiExpandRepeats, exportRPNs, size);

    return packData(buffer.data(), size);
}

/**
 * export score as AudioFile (wav/ogg)
 */
const char* _saveAudio(uintptr_t score_ptr, const char* format, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    // file format of the output file
    // "wav", "ogg", "flac", or "mp3"
    QString _format = QString::fromUtf8(format);
    if (!(_format == "wav" || _format == "ogg" || _format == "flac" || _format == "mp3")) {
        throw QString("Invalid output format");
    }

    // save audio data to a temporary file
    QTemporaryFile tempfile("XXXXXX." + _format);  // filename template for the temporary file
    if (!tempfile.open()) {
        throw QString("Cannot create a temporary file");
    }

    auto filename = tempfile.fileName();
    Ms::saveAudio(score, filename);

    auto size = tempfile.size();
    auto data = tempfile.readAll();
    qDebug("saveAudio: excerpt %d, tempfile %s, size %lld", excerptId, qPrintable(filename), size);

    // delete the temporary file
    tempfile.remove();

    return packData(data, size);
}

/**
 * synthesize audio frames
 */
uintptr_t _synthAudio(uintptr_t score_ptr, float starttime, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    qDebug("synthAudio: excerpt %d, starttime %f", excerptId, starttime);

    score->synthFn = Ms::synthAudioWorklet(score, starttime);

    return score->synthFn == nullptr ? 0 : reinterpret_cast<uintptr_t>(&score->synthFn);
}

const char* _processSynth(uintptr_t fn_ptr, bool cancel) {
    auto fn = reinterpret_cast<std::function<Ms::SynthRes*(bool)>*>(fn_ptr);
    const auto res = (*fn)(cancel);
    return reinterpret_cast<const char*>(res);
}

const char* _processSynthBatch(uintptr_t fn_ptr, int batchSize, bool cancel) {
    auto fn = reinterpret_cast<std::function<Ms::SynthRes*(bool)>*>(fn_ptr);
    // qWarning("size of a pointer: %d", sizeof(Ms::SynthRes*));
    auto resArr = (Ms::SynthRes**)calloc(batchSize, sizeof(Ms::SynthRes*)); // array of pointers to SynthRes data 
    for (size_t i = 0; i < batchSize; i++) {
        resArr[i] = (*fn)(cancel);
    }
    return reinterpret_cast<const char*>(resArr);
}

/**
 * save positions of measures or segments (if the `ofSegments` param == true) as JSON
 */
const char* _savePositions(uintptr_t score_ptr, bool ofSegments, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    score->switchToPageMode();
    QJsonObject json = Ms::savePositions(score, ofSegments);
    QJsonDocument saveDoc(json);

    auto data = saveDoc.toJson(QJsonDocument::Compact);  // UTF-8 encoded JSON data
    qDebug("savePositions: excerpt %d, ofSegments %d, file size %d", excerptId, ofSegments, data.size());

    // JSON is plain text
    return reallocData(data);
}

/**
 * save score metadata as JSON
 */
const char* _saveMetadata(uintptr_t score_ptr) {
    Ms::MasterScore* score = reinterpret_cast<Ms::MasterScore*>(score_ptr);

    QJsonObject json = saveMetadataJSON(score);
    QJsonDocument saveDoc(json);

    // JSON is plain text
    return reallocData(
        saveDoc.toJson()  // UTF-8 encoded JSON data
    );
}

/**
 * export functions (can only be C functions)
 */
extern "C" {

    EMSCRIPTEN_KEEPALIVE
    int version() {
        return _version();
    };

    EMSCRIPTEN_KEEPALIVE
    void init(int argc, char** argv) {
        return _init(argc, argv);
    };

    EMSCRIPTEN_KEEPALIVE
    bool addFont(const char* fontPath) {
        return _addFont(fontPath);
    };

    EMSCRIPTEN_KEEPALIVE
    uintptr_t load(const char* format, const char* data, const uint32_t size, bool doLayout = true) {
        return _load(format, data, size, doLayout);
    };

    EMSCRIPTEN_KEEPALIVE
    void generateExcerpts(uintptr_t score_ptr) {
        return _generateExcerpts(score_ptr);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* title(uintptr_t score_ptr) {
        return _title(score_ptr);
    };

    EMSCRIPTEN_KEEPALIVE
    int npages(uintptr_t score_ptr, int excerptId) {
        return _npages(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveXml(uintptr_t score_ptr, int excerptId = -1) {
        return _saveXml(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveMxl(uintptr_t score_ptr, int excerptId = -1) {
        return _saveMxl(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveMsc(uintptr_t score_ptr, bool compressed, int excerptId = -1) {
        return _saveMsc(score_ptr, compressed, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveSvg(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, int excerptId = -1) {
        return _saveSvg(score_ptr, pageNumber, drawPageBackground, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* savePng(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, bool transparent, int excerptId = -1) {
        return _savePng(score_ptr, pageNumber, drawPageBackground, transparent, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* savePdf(uintptr_t score_ptr, int excerptId = -1) {
        return _savePdf(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveMidi(uintptr_t score_ptr, bool midiExpandRepeats, bool exportRPNs, int excerptId = -1) {
        return _saveMidi(score_ptr, midiExpandRepeats, exportRPNs, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveAudio(uintptr_t score_ptr, const char* format, int excerptId = -1) {
        return _saveAudio(score_ptr, format, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    uintptr_t synthAudio(uintptr_t score_ptr, float starttime, int excerptId = -1) {
        return _synthAudio(score_ptr, starttime, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* processSynth(uintptr_t fn_ptr, bool cancel = false) {
        return _processSynth(fn_ptr, cancel);
    }

    EMSCRIPTEN_KEEPALIVE
    const char* processSynthBatch(uintptr_t fn_ptr, int batchSize, bool cancel = false) {
        return _processSynthBatch(fn_ptr, batchSize, cancel);
    }

    EMSCRIPTEN_KEEPALIVE
    const char* savePositions(uintptr_t score_ptr, bool ofSegments, int excerptId = -1) {
        return _savePositions(score_ptr, ofSegments, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveMetadata(uintptr_t score_ptr) {
        return _saveMetadata(score_ptr);
    };

    EMSCRIPTEN_KEEPALIVE
    void destroy(uintptr_t score_ptr) {
        delete (Ms::MasterScore*)score_ptr;
    };

}
