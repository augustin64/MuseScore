
import WebMscore from 'webmscore4'
import fs from 'fs'

Error.stackTraceLimit = 100;

// https://musescore.com/openscore/scores/4074271
// public domain
const name = 'Aequale_No_1.mscz'

const filedata = fs.readFileSync(`./${name}`);
const soundfont = fs.readFileSync(`../share/sound/FluidR3Mono_GM.sf3`);

WebMscore.ready.then(async () => {
    // await WebMscore.setLogLevel(2);
    console.log('supported file format version:', await WebMscore.version())

    let doSynth = async (score) => {
        await score.setSoundFont(soundfont);
        let synthFn = await score.synthAudio(0);
        let res = await synthFn();
        while(!res.done) {
            res = await synthFn();
        }
    };

    await (async () => {
        const score = await WebMscore.load('mscz', filedata)
        console.log(score)
        console.log()

        console.log('score title:', await score.title())
        console.log('number of pages:', await score.npages())
        console.log()

        let synth = doSynth(score);
        console.log('synth done');
        console.log('audio params:', await score.getAudioOutputParams());
        await score.setAudioOutputParams({master: {volume: -15}, tracks: [{trackId: 3, volume: 15}]});
        await synth;
    })();

    await (async () => {
        const score = await WebMscore.load('mscz', filedata)
        console.log(score)
        console.log()

        console.log('score title:', await score.title())
        console.log('number of pages:', await score.npages())
        console.log()

        console.log('audio params:', await score.getAudioOutputParams());
        await score.setAudioOutputParams({master: {volume: -15}, tracks: [{trackId: 3, volume: 15}]});
        await doSynth(score);
        console.log('synth done');
    })();
})