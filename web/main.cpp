#include <emscripten/emscripten.h>

#include <QGuiApplication>

#include "modularity/ioc.h"

#include "global/io/internal/filesystem.h"
#include "fonts/fontsmodule.h"
#include "draw/drawmodule.h"

#include "engraving/engravingmodule.h"
#include "engraving/libmscore/score.h"
#include "engraving/compat/scoreaccess.h"

#include "importexport/guitarpro/guitarpromodule.h"

/**
 * MSCZ/MSCX file format version
 */
int _version() {
    return mu::engraving::MSCVERSION;
}

/**
 * init libmscore
 */
void _init(int argc, char** argv) {
    new QGuiApplication(argc, argv);

    // src/framework/global/globalmodule.cpp#67
    mu::modularity::ioc()->registerExport<mu::io::IFileSystem>("", new mu::io::FileSystem());

    auto fontsM = new mu::fonts::FontsModule();
    fontsM->registerResources();
    // src/framework/draw/drawmodule.cpp
    auto drawM = new mu::draw::DrawModule();
    drawM->registerExports();

    auto engM = new mu::engraving::EngravingModule();
    engM->registerResources();
    engM->registerExports();
    engM->onInit(mu::framework::IApplication::RunMode::Converter);

    mu::engraving::compat::ScoreAccess::createMasterScore();

    // mu::iex::guitarpro::GuitarProModule gm;
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
}
