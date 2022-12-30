#include <emscripten/emscripten.h>

#include "modularity/ioc.h"

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
    // mu::modularity::ioc()->registerExport<mu::draw::IFontProvider>("test", new mu::draw::FontProviderStub());

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
