#include <emscripten/emscripten.h>

#include <QGuiApplication>
#include <QFontDatabase>
#include <QTemporaryFile>
#include "global/io/buffer.h"

#include "modularity/ioc.h"
#include "global/io/internal/filesystem.h"
#include "global/internal/cryptographichash.h"
#include "fonts/fontsmodule.h"
#include "draw/drawmodule.h"
#include "engraving/engravingmodule.h"

#include "draw/ifontprovider.h"
#include "engraving/libmscore/score.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/writescorehook.h"
#include "engraving/libmscore/excerpt.h"
#include "engraving/libmscore/undo.h"

using namespace mu;

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
    // TODO: refactor to `io::ByteArray` and `io::Buffer`
    QByteArray sizeData = QByteArray((const char*)&size, 4);

    QBuffer result;
    result.open(QIODevice::ReadWrite);
    result.write(sizeData);
    result.write(data);
    result.close();

    return reallocData(result.data());
}

engraving::MasterScore* maybeUseExcerpt(engraving::MasterScore* score, int excerptId) {
    // -1 means the full score
    if (excerptId < 0) {
        return score;
    }

    // excerptId >= 0
    auto excerpts = score->excerpts();

    if (excerptId >= excerpts.size()) {
        throw(QString("Not a valid excerptId."));
    }

    qDebug("useExcerpt: %d", excerptId);
    return (engraving::MasterScore*) excerpts[excerptId]->excerptScore();
}

/**
 * MSCZ/MSCX file format version
 */
int _version() {
    return engraving::MSCVERSION;
}

/**
 * init libmscore
 */
void _init(int argc, char** argv) {
    new QGuiApplication(argc, argv);

    // src/framework/global/globalmodule.cpp#67
    modularity::ioc()->registerExport<io::IFileSystem>("", new io::FileSystem());
    modularity::ioc()->registerExport<ICryptographicHash>("", new CryptographicHash());

    auto fontsM = new fonts::FontsModule();
    fontsM->registerResources();
    // src/framework/draw/drawmodule.cpp
    auto drawM = new draw::DrawModule();
    drawM->registerExports();

    auto engM = new engraving::EngravingModule();
    engM->registerResources();
    engM->registerExports();
    engM->onInit(framework::IApplication::RunMode::Converter);
}

/**
 * load (CJK) fonts on demand
 */
bool _addFont(const char* fontPath) {
    String _fontPath = String::fromUtf8(fontPath);
    auto fontProvider = modularity::ioc()->resolve<draw::IFontProvider>("");

    if (-1 == fontProvider->addTextFont(_fontPath)) {
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
    String _format = String::fromUtf8(format);  // file format of the data

    engraving::MasterScore* score = engraving::compat::ScoreAccess::createMasterScore();

    // create a temporary file, and write `data` into it
    QTemporaryFile tempfile("XXXXXX." + _format);  // filename template for the temporary file
    if (!tempfile.open()) { // a QTemporaryFile will always be opened in `QIODevice::ReadWrite` mode
        throw QString("Cannot create a temporary file");
    } else {
        tempfile.write(data, size);
        tempfile.close(); // calls QFileDevice::flush() and closes the file
    }
    QString name = tempfile.fileName(); // temporary filename

    engraving::Err rv = engraving::compat::loadMsczOrMscx(score, name, true);

    // delete the temporary file
    tempfile.remove();

    // handle exceptions
    if (rv != engraving::Err::NoError) {
        return char(rv);
    }

    // post processing for non-native formats
    if (!(_format == "mscz" || _format == "mscx")) {
        score->setMetaTag(u"originalFormat", _format);
        score->connectTies();
    }

    // mscore/file.cpp#L2387 readScore
    score->rebuildMidiMapping();
    score->setSoloMute();
    for (auto s : score->scoreList()) {
        s->setPlaylistDirty();
        s->addLayoutFlags(engraving::LayoutFlag::FIX_PITCH_VELO);
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
    auto score = reinterpret_cast<engraving::MasterScore*>(score_ptr);

    auto scoreExcerpts = score->excerpts();
    if (scoreExcerpts.size() > 0) {
        // has existing excerpts
        return;
    }

    auto parts = score->parts();
    auto excerpts = engraving::Excerpt::createExcerptsFromParts(parts);

    // TODO: testing
    // https://github.com/LibreScore/webmscore/blob/v4.0/src/engraving/libmscore/unrollrepeats.cpp#L99-L117
    for (auto e : excerpts) {
        engraving::Score* nscore = e->masterScore()->createScore();
        e->setExcerptScore(nscore);
        nscore->style().set(engraving::Sid::createMultiMeasureRests, true);
        auto excerptCmdFake = new engraving::AddExcerpt(e);
        excerptCmdFake->redo(nullptr);
        engraving::Excerpt::createExcerpt(e);

        // add this excerpt back to the score excerpt list
        scoreExcerpts.push_back(e);
    }

    qDebug("Generated excerpts: size %d", excerpts.size());
}

/**
 * get the score title
 */
const char* _title(uintptr_t score_ptr) {
    auto score = reinterpret_cast<engraving::MasterScore*>(score_ptr);

    // code from MuseScore::saveMetadataJSON
    // https://github.com/LibreScore/webmscore/blob/d1259f64/mscore/file.cpp#L3232-L3241
    QString title;
    engraving::Text* t = score->getText(engraving::TextStyleType::TITLE);
    if (t)
        title = t->plainText();
    if (title.isEmpty())
        title = score->metaTag(u"workTitle");
    if (title.isEmpty())
        title = score->name();

    return reallocData(
        title.toUtf8()
    );
}

/**
 * get the number of pages
 */
int _npages(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<engraving::MasterScore*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);
    return score->npages();
}
/**
 * save part score as MSCZ/MSCX file
 */
const char* _saveMsc(uintptr_t score_ptr, bool compressed, int excerptId) {
    auto score = reinterpret_cast<engraving::MasterScore*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    if (!score->isMaster()) {  // clone metaTags from masterScore
        auto j(score->masterScore()->metaTags());
        for (auto p : j) {
            if (p.first != "partName")  // don't copy "partName" should that exist in masterScore
                score->metaTags().insert({p.first, p.second});
            score->metaTags().insert({u"platform", u"webmscore"});
            score->metaTags().insert({u"source", u"https://github.com/LibreScore/webmscore"});
            score->metaTags().insert({u"creationDate", Date::currentDate().toString()});  // update "creationDate"
        }
    }

    io::Buffer buffer;
    buffer.open(io::IODevice::ReadWrite);

    engraving::compat::WriteScoreHook hook;
    compressed = 0; // FIXME: 
    if (compressed) {
        // score->saveCompressedFile(&buffer, "score.mscx", false, true);
    } else {
        score->writeScore(&buffer, false, false, hook);
    }

    if (!score->isMaster()) {  // remove metaTags added above
        auto j(score->masterScore()->metaTags());
        for (auto p : j) {
            // remove all but "partName", should that exist in masterScore
            if (p.first != "partName")
                score->metaTags().erase(p.first);
        }
    }

    auto size = buffer.size();
    qDebug("saveMsc: compressed %d, excerpt %d, size %lld", compressed, excerptId, size);

    return packData(buffer.data().toQByteArrayNoCopy(), size);
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
        delete (engraving::MasterScore*)score_ptr;
    };

}
