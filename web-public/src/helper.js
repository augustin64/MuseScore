
import LibMscore from '../webmscore.lib.js'
import { IS_NODE, getSelfURL } from './utils.js'

const moduleOptions = IS_NODE
    ? {
        locateFile(path) {
            const { join } = require('path')
            return join(__dirname, path)
        },
        getPreloadedPackage(remotePackageName) {
            const buf = require('fs').readFileSync(remotePackageName).buffer
            return buf
        }
    }
    : {
        locateFile(path) {
            // %INJECTION_HINT_0%
            // fix loading the preload pack in Browsers and WebWorkers
            const prefix = typeof MSCORE_SCRIPT_URL == 'string'
                ? MSCORE_SCRIPT_URL  // to use like an environment variable
                : getSelfURL()
            return new URL(path, prefix).href
        }
    }

/** @type {Record<string, any>} */
let Module = moduleOptions
/** @type {Promise<any>} */
const ModulePromise = LibMscore(moduleOptions)
export { Module }

/**
 * get the pointer to a js string, as utf-8 encoded char*
 * @param {string} str 
 * @returns {number}
 */
export const getStrPtr = (str) => {
    const maxSize = str.length * 4 + 1
    const buf = Module._malloc(maxSize)
    Module.stringToUTF8(str, buf, maxSize)
    return buf
}

/**
 * get the pointer to a TypedArray, as char*
 * @typedef {Int8Array | Int16Array | Int32Array | Uint8Array | Uint16Array | Uint32Array | Uint8ClampedArray | Float32Array | Float64Array} TypedArray
 * @param {TypedArray} data 
 * @returns {number}
 */
export const getTypedArrayPtr = (data) => {
    const size = data.length * data.BYTES_PER_ELEMENT
    const buf = Module._malloc(size)
    Module.HEAPU8.set(data, buf)
    return buf
}

export class WasmRes {
    /** @type {number} */
    _ptr
    /** @type {number} */
    _size

    /**
     * Read responses from the wasm module
     * @param {number} ptr char* pointer to the responses data
     */
    constructor(ptr) {
        this._ptr = ptr

        const sizeData = new DataView(
            new Uint8Array(  // make a copy
                Module.HEAPU8.subarray(ptr, ptr + 4)
            ).buffer
        )
        this._size = sizeData.getUint32(0, true)
    }

    /**
     * pointer to the data contents 
     * @private
     */
    get _dataPtr() {
        return this._ptr + 4
    }

    /**
     * Read the data contents as Uint8Array
     * @returns {Uint8Array}
     */
    data() {
        return new Uint8Array( // make a copy
            Module.HEAPU8.subarray(this._dataPtr, this._dataPtr + this._size)
        )
    }

    /**
     * Read the data contents as UTF-8 string
     * @returns {string}
     */
    text() {
        return Module.UTF8ToString(this._dataPtr)
    }

    /**
     * Read the data contents as number
     * @returns {number}
     */
    number() {
        return +this.text()
    }

    free() {
        return freePtr(this._ptr)
    }

    /**
     * @private 
     * @param {number} ptr 
     * @param {keyof WasmRes} method
     */
    static _readAndFree(ptr, method) {
        const res = new WasmRes(ptr)
        const s = res[method]()
        res.free()
        return s
    }

    /**
     * read wasm responses as Uint8Array  
     * @param {number} ptr 
     * @returns {Uint8Array}
     */
    static readData(ptr) {
        return WasmRes._readAndFree(ptr, 'data')
    }

    /**
     * read wasm responses as UTF-8 string 
     * @param {number} ptr 
     * @returns {string}
     */
    static readText(ptr) {
        return WasmRes._readAndFree(ptr, 'text')
    }

    /**
     * read wasm responses as number
     * @param {number} ptr 
     * @returns {number}
     */
    static readNum(ptr) {
        return WasmRes._readAndFree(ptr, 'number')
    }
}

/**
 * free a pointer
 * @param {number} bufPtr 
 */
export const freePtr = (bufPtr) => {
    Module._free(bufPtr)
}

/**
 * this promise is resolved when the runtime is fully initialized
 */
export const RuntimeInitialized = new Promise((resolve) => {
    ModulePromise.then((_Module) => {
        Module = _Module
        Module.ccall('init')  // init libmscore
        resolve(undefined)
    })
})

/**
 * @enum {number}
 * @see libmscore/score.h#L396-L410
 */
export const FileErrorEnum = Object.assign([
    // error code -> error name
    'FILE_NO_ERROR',
    'FILE_ERROR',
    'FILE_NOT_FOUND',
    'FILE_OPEN_ERROR',
    'FILE_BAD_FORMAT',
    'FILE_UNKNOWN_TYPE',
    'FILE_NO_ROOTFILE',
    'FILE_TOO_OLD',
    'FILE_TOO_NEW',
    'FILE_OLD_300_FORMAT',
    'FILE_CORRUPTED',
    'FILE_USER_ABORT',
    'FILE_IGNORE_ERROR',
], {
    // error name -> error code
    // make up TypeScript-like enum manually
    'FILE_NO_ERROR': 0,
    'FILE_ERROR': 1,
    'FILE_NOT_FOUND': 2,
    'FILE_OPEN_ERROR': 3,
    'FILE_BAD_FORMAT': 4,
    'FILE_UNKNOWN_TYPE': 5,
    'FILE_NO_ROOTFILE': 6,
    'FILE_TOO_OLD': 7,
    'FILE_TOO_NEW': 8,
    'FILE_OLD_300_FORMAT': 9,
    'FILE_CORRUPTED': 10,
    'FILE_USER_ABORT': 11,
    'FILE_IGNORE_ERROR': 12,
})

export class FileError extends Error {
    /**
     * @param {FileErrorEnum} errorCode 
     */
    constructor(errorCode) {
        super()
        this.name = 'FileError'
        this.errorCode = errorCode
        this.errorName = FileErrorEnum[errorCode] || FileErrorEnum[1]
        this.message = `WebMscore: ${this.errorName}`
    }
}
