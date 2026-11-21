# LODESTONE

**Every note finds home.**

Lodestone is real-time pitch correction, harmony and doubling from LowKei Audio. It hears the voice, finds the note it is reaching for, and pulls it onto the scale as fast or as gently as you like: from barely-there polish to the hard-tuned robot effect. It keeps the singer's own tone while it does it, and builds up to three in-key harmonies and a double from the same performance. From version 2 it can also listen to the beat: **Chord Lock** pulls the voice onto the chord that is playing, and **Motion** turns each held note into a tempo-synced pattern (trills, arps and octave hops, in key).

- **Formats:** VST3 and CLAP (Windows), plus a Windows standalone app. AU is added automatically if built on a Mac.
- **Hosts:** Serato Studio, FL Studio and Ableton Live load the VST3. FL Studio, Bitwig Studio and Reaper also load the CLAP.
- **Views:** **Easy** (key, the live field, SNAP and HOLD, the main tuning knobs and the macros) and **Pro** (every control, harmonies, the beat, modulation and global settings).
- **Inputs:** the voice, plus an optional **sidechain** for the beat (Chord Lock and LEARN listen to it).
- **Presets:** 57 factory presets in 14 categories, each with its four macros assigned.
- **Version:** 2.0.0

## Changes

| Version | What changed |
|---|---|
| 2.0.0 | **New: Chord Lock.** Route the beat to Lodestone's new sidechain input and it hears the chords: Chord Lock pulls sung notes onto the chord that is playing (50%: a note a semitone off a chord tone moves to it; 100%: chord tones only), and from 50% up the harmonies stack on the chord's own notes, so they never clash with the beat. Chords are named about 0.2 s after they change; drums and noise never become a chord, and the last chord is kept through drum-only bars. **LEARN** listens to the beat when one is connected (far more reliable than the voice, and done in about 5 s). **New: Motion**, a 16-step note-pattern sequencer synced to the tempo: each step moves the note up or down the scale (or, with Chord Lock at 100%, through the chord), with Glide, Rate, Steps and a Grid or Phrase start, plus ten ready-made patterns. **New BEAT page** in the Pro view; Motion has a switch in the Easy view's Voices. **10 new presets** (Beat and Motion categories). **Tuning fixes**, all measured: legato leaps now land in 12-41 ms (1.0.1: 35-122 ms), and the voice is no longer held on the old note while a leap is confirmed. A leap could briefly land on a wrong note in between (for example 64 on a 60 to 65 leap); that can no longer happen. Octave leaps are followed in every Input range: in 1.0.1 an upward octave leap on the Alto/Tenor, Low Male, Instrument and Bass ranges stayed on the old octave for the rest of the note. A rare click when the correction glided across "no change" (constant with Motion trills) is gone. Typing "+3" into Transpose (or any whole-number control) now reads as +3, not 0. Projects and presets from 1.x open with Chord Lock at 0 and Motion off; they sound the same except where the fixes above apply. |
| 1.0.1 | **Static and clicks fixed** (reported in Serato Studio for the whole LowKei Audio family). **Preset changes** (the preset menu, Dice, A/B, undo and redo of a preset load, and the host loading a project) are now seamless: Lodestone dips to your dry voice for about 50 ms, swaps every setting at once and fades the new sound in. Before, settings changed one by one under the voice, and some switches clicked. **Loops and restarts:** tempo-synced LFOs ease onto the new beat position when a loop wraps round or the transport restarts over sound, instead of snapping. Starting from silence is still exact, so playback matches a bounce. **Knobs and automation:** Lead, Breath and Grit used to jump in a single sample when moved quickly or automated in steps, which clicked (Lead was the worst). They now glide over about 12 ms. Throat, the harmonies' Formant and Character glide over about 20 ms, so they no longer click when they change quickly (by hand, by a macro or by a synced LFO on a loop). New regression checks cover all of this. Saved projects load as before, and 46 of the 47 factory presets render bit for bit the same as in 1.0.0; Throat Sweep differs only by the new 20 ms smoothing of its LFO-driven Throat. |
| 1.0.0 | First release. |

---

## What it does that others don't

| Feature | What it means for you |
|---|---|
| **LEARN key** | Built in, free: play the song or sing over it and Lodestone sets the Key and Scale itself. |
| **Note Stability** | Vibrato, scoops and slides never flip the voice between two notes. A new note must win clearly, and for long enough. |
| **Bounce** | The pull can overshoot and spring back on every note change, from a subtle flutter to a yodel. |
| **Three smart harmonies + a double** | Harmonies follow the scale (a "3rd up" is always in key), each with its own throat and pan, and a little humanised drift so they sound sung, not cloned. All built from one analysis, so CPU stays low. |
| **MIDI four ways** | Off, **Scale** (held notes become the scale), **Exact** (sing anything, it lands on the note you play) or **Harmony** (play chords: each held note becomes a harmony voice). |
| **Micro-tuning** | Every note of the scale can be tuned up or down by up to 50 cents: blue notes, maqam, just intonation. |
| **SNAP and HOLD** | Performance buttons: SNAP throws instant hard tune onto a word; HOLD freezes the current note. Both automatable. |
| **Grit** | A ring modulator locked to the voice's pitch: sub rumble, growl, buzz or metal. |
| **Live and Studio latency** | Live (3 ms) for singing through it; Studio (15 ms, compensated by the DAW) for exact timing when mixing. |
| **Chord Lock** (2.0) | Listens to the beat on the sidechain and pulls the voice onto the chord that is playing; the harmonies stack on the chord. No other tuner hears the beat. |
| **Motion** (2.0) | A tempo-synced pattern for every held note: trills, arpeggios, octave hops, in key (or through the chord). |

---

## Install (Windows)

1. Download **Lodestone-2.0.0-Windows-Setup.exe** from the repository's **Releases** page.
2. Run it and choose **VST3 and CLAP plug-ins and standalone app** or **Plug-ins only**. The installer places the VST3 in `C:\Program Files\Common Files\VST3` and the CLAP in `C:\Program Files\Common Files\CLAP`, the standard folders every supported DAW scans, and adds Start menu shortcuts for the app and this guide.
3. Rescan in your DAW:

| DAW | Steps |
|---|---|
| **Serato Studio** | Setup (cog icon) > **Plugins**. Enable **Default VST Folder**, then click **Scan**. Turn on **Show FX Panel** for a deck or track, choose **Plugins > All Effects** in the Library panel, and drag Lodestone onto an FX slot. |
| **FL Studio** | Options > File settings > **Manage plugins**. Click **Find installed plugins** with **Verify plugins** ticked. Lodestone appears under Plugin database > Installed > Effects > New. Versions of FL Studio with CLAP support list both a VST3 and a CLAP entry; either works, so use one per project. |
| **Ableton Live** | Options > Preferences > **Plug-Ins**. Turn **Use VST3 Plug-In System Folders** on, then click **Rescan** (hold **Alt** while clicking for a full rescan). Lodestone appears under **Plug-Ins** in the browser. |

**Uninstall:** Windows Settings > Apps > Installed apps > **Lodestone** > Uninstall. This removes the plug-ins and the app. Your saved presets in `Documents\LowKei Audio\Lodestone\Presets` are kept.

**Manual install (no installer):** download **Lodestone-Windows-x64** from the latest green **Build** run on the **Actions** tab, then copy the whole **`Lodestone.vst3`** folder (not just the file inside it) to `C:\Program Files\Common Files\VST3`, and **`Lodestone.clap`** to `C:\Program Files\Common Files\CLAP` (create the folder if it does not exist).

**Standalone app:** open **Lodestone** from the Start menu. Click **Options > Audio/MIDI Settings** to pick your audio interface, your microphone input and (optionally) a MIDI keyboard. Set **LATENCY** to **LIVE** on the GLOBAL page when you sing through it.

No Visual C++ Redistributable is needed; the runtime is built in.

---

## Quick start

1. Put Lodestone on a **single vocal** (one voice at a time: it follows one melody).
2. Set **Key** and **Scale** to the song, or click **LEARN** and play a few bars: it sets them for you.
3. Pick **Input** to match the singer (Soprano, Alto/Tenor, Low Male, Instrument or Bass Instrument).
4. Turn **Speed**: slower is natural, faster is tighter, 0 ms is the robot effect.

That's it. Everything else shapes the result.

---

## How it works

```
voice --> pitch tracker --> which note? (Key, Scale, MIDI, Stability)
    \                             |
     \                    how hard, how fast? (Speed, Humanize, Flex, Amount, Bounce, Vibrato)
      \                           |
       +--> pitch-synchronous grains --> lead (tuned, own throat)
                                    +--> 3 harmonies (in key, panned, own throats)
                                    +--> double (two takes, early / late, wide)
                                         --> Grit --> Mix, Width, Output, Safety limiter
```

Lodestone tracks the pitch of the voice period by period, then rebuilds the voice one glottal pulse at a time at the new pitch (TD-PSOLA). Because each pulse keeps its shape, the voice keeps its own formants: a corrected note still sounds like the singer, not a chipmunk. **Classic** character lets the tone move with the pitch for the old-school sound. Breaths and consonants (the parts with no pitch) pass straight through, at the **Breath** level.

---

## Easy view: tune in ten seconds

| Control | What it does |
|---|---|
| **The field** | The live display in the middle. **Blue** is the voice as sung, **gold** is the voice as tuned; lit lanes are the target notes, and the note being sung glows. With Chord Lock in use, the chord's notes have **cyan** lane lines and a cyan chip names the chord; with Motion on, a lime chip shows the step playing. Where gold leaves blue, Lodestone is pulling. At the right edge, coloured dots show the harmony voices. The readout shows the target note, how far off it was **sung** and where it was **tuned** to, with a tuner needle (blue: sung, gold: tuned). "Flex: left alone" appears when Flex is letting an expressive move through. |
| **Keyboard** | One octave under the field. Lit keys are the target notes; a ring marks the note being sung; a dot marks a held MIDI note. **Click a key** to add or remove it: the Scale switches to **Custom**, starting from the notes you had. |
| **Key / Scale** | The song's key and scale. 15 scales plus Custom: Chromatic, Major, Minor, Harmonic Minor, Melodic Minor, Major and Minor Pentatonic, Blues, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Double Harmonic and Whole Tone. |
| **LEARN** | Click, then play the song (or sing over it). Lodestone shows what it hears ("Hearing A minor 78%"), sets Key and Scale on its own once it is sure, or when you click **SET KEY**. |
| **Input** | The voice's range. Matching it makes tracking faster and more reliable. |
| **Character** | **Modern** keeps the singer's natural tone at any pitch. **Classic** lets the tone shift with the pitch (the late-90s sound, and chipmunk / monster effects). |
| **Voices** | One-click power for **Correction**, **Vibrato**, **Harmony 1-3**, **Double** and **Motion**. With Correction off, the voice passes untouched and the harmonies, double and Motion still play. |
| **Transpose / Tracking** | Transpose moves the output by semitones. Tracking: higher follows quieter and breathier notes; lower only clear, strong ones. |
| **SNAP** | Instant hard tune while engaged: 0 ms, no vibrato, no flex. Throw it onto single words. |
| **HOLD** | Freezes the note being sung: whatever comes next lands on that note until you let go. |
| **MOMENT / LATCH** | Under each button. **MOMENT**: only while pressed. **LATCH**: click on, click off. Right-click a button for the same choice. Both are parameters, so you can automate them. |
| **Speed** | How fast notes are pulled in tune. 40-80 ms: natural. 10-25 ms: modern pop. 0 ms: the robot effect. |
| **Humanize** | Long notes are pulled more gently so they keep their life; short notes stay tight. |
| **Flex** | Leaves expressive moves alone: only notes sung close to a target are corrected. Scoops, falls and slides stay as sung. |
| **Vibrato** | How much of the singer's own vibrato survives: 0% flat, 100% as sung, 200% exaggerated. |
| **Throat** | Throat length: down is longer, deeper and bigger; up is shorter, younger and smaller. The pitch does not change. |
| **Stability** | How sure Lodestone must be before it changes note. Higher stops vibrato and scoops from flipping notes; lower follows fast runs. |
| **Macros** | **Pull** (faster, firmer, more robotic), **Throat** (longer throat and a little grit), **Choir** (brings up the harmonies and double that are on) and **Width** (spreads the double and the voices). The line under each macro shows how many targets it moves. |
| **Mix / Output** | Blend of the untouched voice and Lodestone (both stay in time); final level. |
| **Dice** (top bar) | **Spin**: a random setting that works on any vocal. It keeps your Key and Scale. |

The bar along the bottom explains whatever is under your mouse. With the mouse over empty space, it shows what the current preset does.

---

## Pro view: every control

Click **PRO** in the top bar. The field, SNAP, HOLD and the macros stay on the right; the left side has six pages.

### TUNE page

**Correction**

| Control | What it does |
|---|---|
| Key, Scale, Input | As in the Easy view. |
| MIDI | **Off**; **Scale** (the notes you hold become the scale; nothing held = Key and Scale); **Exact** (the voice lands on the note you play, whatever was sung); **Harmony** (each held note, up to three, becomes a harmony voice around the lead). |
| Tracking | Pitch-tracking sensitivity. |
| A = | Reference pitch, 415 to 466 Hz (440 Hz standard). |
| Speed, Humanize, Flex | As in the Easy view. |
| Amount | How much of the way to the note it pulls (100% = all the way). Lower for gentle, partial correction. |
| Bounce | The pull overshoots and springs back on note changes. Small amounts add a flutter; high amounts with a slower Speed make a yodel. |
| Stability | As in the Easy view. |

**Scale Notes & Micro-Tuning**

| Control | What it does |
|---|---|
| LEARN | As in the Easy view. |
| Keyboard | Click keys to build a custom scale. |
| C to B knobs | Tune each note's target up or down by up to 50 cents. The keyboard shows the offset under each key. |

### VOICE page

**Character**: Character (Modern / Classic), **Throat**, **Vibrato** (natural vibrato kept), **Breath** (level of breaths and consonants, 0 to 150%), **Lead** (level of the tuned lead: 0 = harmonies and double only), **Transpose**, **Fine** (cents), **Grit** and **Grit Type** (**Sub**: an octave-down rumble, **Growl**, **Buzz**, **Metal**).

**Vibrato** (added): **Depth** (up to 100 cents), **Rate** (1 to 10 Hz), **Delay** (each note starts straight for this long), **Onset** (then the vibrato grows in over this long) and **Variation** (random wander in rate and depth, like a real singer). Use it with Vibrato (kept) at 0% to replace the singer's vibrato with a perfect one.

### HARMONY page

| Section | Controls |
|---|---|
| **Harmony 1, 2, 3** | **Interval**: named intervals follow the scale (**3rd Up**, **4th Up**, **5th Up**, **6th Up**, **3rd Down**, **4th Down**, **5th Down**, **6th Down**, **Oct + 3rd**, **Oct + 5th**); **Oct Up**, **Oct Down**, **Unison** and the "st" ones are fixed semitones. **Level**, **Pan** and **Throat** (its own throat: up for a lighter singer, down for a deeper one). |
| **Double** | Two extra takes of the lead, one early and one late, one a little sharp and one a little flat: **Level**, **Spread** (how far apart they are panned), **Detune** (cents) and **Delay** (ms). |
| **Ensemble** | **Humanize**: tiny timing and pitch drift on the harmonies so they sound sung. **Width**: stereo width of the harmonies and double. |

Harmonies follow the tuned lead. With **Unison** and a Throat setting, a harmony becomes a second singer on the same line.

### BEAT page (2.0)

**Chord Lock: Follow the Beat**

| Control | What it does |
|---|---|
| **Chord** | The chord the beat is playing ("Am7", "F", "Gsus4"), the sidechain's status and the beat's notes as 12 bars (the chord's notes lit). Recognised chords: major, minor, 7, m7, maj7, dim and sus4. |
| **Chord Lock** | 0%: the scale as set. Above 0%: the chord's notes are preferred, and they are allowed even when they are outside the scale (a secondary dominant's raised note, for example). 50%: a note sung a semitone off a chord tone moves onto it. 100%: chord tones only. From 50% up, the harmonies stack on the chord's notes ("3rd Up" = the next chord note up, "5th Up" = the one after). |

Chord Lock needs the beat on Lodestone's **sidechain input**. With no sidechain it does nothing, and Lodestone uses the Key and Scale as set. MIDI Mode **Scale** (with keys held) wins over the beat.

| DAW | Routing the beat to the sidechain |
|---|---|
| **FL Studio** | Select the beat's mixer track, then right-click the arrow at the bottom of Lodestone's (vocal) mixer track and choose **Sidechain to this track**. In Lodestone's wrapper, open the gear icon > **Processing** tab, and under Connections set the sidechain input to the beat's track number. |
| **Ableton Live** | In Lodestone's device panel (the small one in the Device View), turn **Sidechain** on and choose the beat's track in **Audio From**. |
| **Serato Studio** | Version 2.1.2 and later: third-party effects that support sidechain can take a Serato Studio deck or pad as their sidechain input. Choose the deck or pads that hold the beat. |

**Motion**

| Control | What it does |
|---|---|
| **Motion** (power) | Turns the pattern on. Also on the Easy view's Voices. |
| **Rate** | How long each step lasts (1/64 up to 4 bars, dotted and triplet). |
| **Steps** | How many steps the pattern has (1 to 16) before it repeats. |
| **Glide** | How smoothly the voice moves from step to step (0 ms jumps). The Pull macro shortens it. |
| **Start** | **Phrase**: the pattern starts on step 1 every time singing starts after a pause. **Grid**: it follows the song's bars, lined up with the delayed output. |
| **PATTERNS** | Ready-made patterns: Up and Down, Trill, Rising Arp, Falling Arp, Octave Hop, Turn, Fifth Stab, Staircase, Low Answer, Triplet Roll, or Clear. One undoable step. |
| **The steps** | Drag up or down to set how many notes each step moves (draw across steps); double-click a step for 0. Steps count notes of the scale: in a 7-note scale +2 is a third, +4 a fifth and +7 an octave. With Chord Lock at 100% they count notes of the chord instead, so a pattern arpeggiates whatever chord the beat plays. |

Harmonies follow the note as Motion moves it, so a harmony stays a third above every step.

### MOD page

| Section | What it does |
|---|---|
| **LFO 1 / LFO 2** | Seven shapes (Sine, Triangle, Saw Up, Saw Down, Square, S&H, Smooth Random), free in Hz or synced to the host. |
| **Envelope** | Follows the input level: Attack, Release and Sensitivity. |
| **Drift** | Random movement, from stepped (0% Smooth) to gliding (100%). |
| **Matrix (16 slots)** | **Source** to **Destination** with a bipolar **Amount**. Sources: LFO 1, LFO 2, Envelope, Drift and the four macros. Destinations are grouped: Global, Tune, Vibrato, Harmonies, Double, Beat (Chord Lock, Motion Glide) and the modulators themselves. **CLEAR ALL** empties the matrix, macros included. |

**Fastest way to modulate:** right-click any knob and choose **Modulate with** (LFO 1, LFO 2, Envelope, Drift) or **Assign to macro**. A thin outer ring on a knob shows its modulation range; a white dot shows where modulation has actually moved it.

### GLOBAL page

| Section | Controls |
|---|---|
| **Levels & Output** | Input (also what the tracker hears), Output, Mix, Width (0 to 200%) and the **Safety Limiter** (-0.3 dBFS ceiling, on by default). |
| **Latency** | **LIVE** (about 3 ms) for singing through Lodestone while recording or on stage. **STUDIO** (about 15 ms) lines the correction up exactly with the notes; the DAW compensates it automatically. Saved with your session, not with presets. |
| **Tempo & Random** | Free Tempo (for synced LFOs when the host sends no tempo, such as in the standalone app) and Seed (same seed = same humanize drift and random modulation every time). |

---

## Tips

- **Natural lead vocal:** start from **Studio Polish**. If long notes sound stiff, raise Humanize; if slides get flattened, raise Flex.
- **The robot effect:** **Robot**, or any preset plus **SNAP**. Lower Stability a little if fast passages hang on a note.
- **Fast runs:** load **Runs** (low Stability) so every note of the run is followed.
- **Wobbly singer, big vibrato:** raise Stability and leave Vibrato at 100%: Lodestone corrects the centre of the note and leaves the vibrato alone.
- **Instant backing vocals:** **Stack of Three** or **Gospel Pad**. Make sure the Key and Scale are right: the harmonies follow them.
- **Chords from a keyboard:** **MIDI Chords**. Lodestone needs MIDI notes sent to it (how you route MIDI to an effect depends on your DAW). If your host cannot send MIDI to an effect, use the scale-based harmonies instead: they follow the Key and Scale.
- **Recording through it:** set LATENCY to **LIVE** and monitor through your DAW. Switch back to **STUDIO** for mixing.
- **Rap and talking parts:** raise Flex (and lower Tracking) so spoken words are left alone.
- **Tuning to the beat:** route the beat to the sidechain, load **Follow the Beat** and press **LEARN**: it sets the Key from the beat in a few seconds, and Chord Lock keeps each note on the chord.
- **A hook from one note:** load **Call and Answer** (or any Motion preset) and hold a note: the pattern sings the melody.

## Factory presets

| Category | Presets |
|---|---|
| Init | Init |
| Natural | Transparent, Studio Polish, Gentle Ballad, Backing Section |
| Pop | Modern Pop, Glossy Lead, Tight Hook |
| Hard Tune | Robot, Classic 98, Flutter, Yodel Jump, Hard But Human |
| Melodic Trap | Melodic Trap, Late Night, Wavy, Drill Tune |
| R&B | Silk, Runs, Neo Soul |
| Harmony | Thirds Above, Power Fifths, Stack of Three, Gospel Pad, Octave Choir, Low Shadow, Sixths Below |
| Double | Wide Double, Tight ADT, Stadium |
| Character | Bigger Singer, Younger, Deep Voice, Chipmunk, Monster, Growler, Metal Voice |
| FX | Warble, Tape Wobble, Robo Choir, Octaver, Throat Sweep |
| Live | Live Stage, Live Robot |
| MIDI | MIDI Melody, MIDI Scale, MIDI Chords |
| Beat | Follow the Beat, Chord Robot, Chord Choir, Beat Hook |
| Motion | Trill Lead, Arp Rider, Octave Hop, Stair Runner, Call and Answer, Chord Arp |

Presets with a key (Melodic Trap, R&B) set one to show off the sound: set the Key and Scale to your song (or LEARN) after loading. The **Beat** presets and **Chord Arp** need the beat on the sidechain; without it they play like their tuning settings alone.

**Saving and comparing:** **SAVE** stores the current sound (every setting plus the modulation matrix) in `Documents\LowKei Audio\Lodestone\Presets`, where you can back up or share the `.lodestone` files. **A / B** keeps two versions of a sound to compare; the preset menu has **Copy A to B**.

## Troubleshooting

| Problem | Fix |
|---|---|
| Notes jump to the wrong note | Check Key and Scale (or LEARN). Raise Stability. |
| The voice warbles between notes | Raise Stability; check Input matches the singer. |
| Breaths or words sound robotic | Lower Tracking so only clear notes are tuned; raise Flex for spoken parts. |
| Harmonies are out of key | The harmonies follow Key and Scale: set them to the song. |
| A slight delay when singing through it | Set LATENCY to LIVE on the GLOBAL page. |
| Nothing happens on a stereo bus or a whole mix | Lodestone follows one voice at a time. Put it on the single vocal track. |
| Chord Lock does nothing | The BEAT page says whether a sidechain is connected and hearing sound. Route the beat to Lodestone's sidechain input (see the BEAT page section) and raise Chord Lock above 0%. |
| The chord shown is a little behind the beat | A chord is named about 0.2 s after it starts (it has to be heard first). Notes sung right on the change follow it a moment later. |
| Motion does not follow the song's bars | Set **Start** to **Grid**. Phrase restarts the pattern whenever singing starts. |

---

## Building from source

Requirements: CMake 3.22+, plus Visual Studio 2022 or newer (Windows), Xcode (macOS), or GCC/Clang (Linux). JUCE 9.0.3, clap-juce-extensions and Catch2 are downloaded automatically.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Lodestone_VST3 Lodestone_CLAP Lodestone_Standalone
```

The plugins are written to `build/src/Lodestone_artefacts/Release/VST3/` and `.../Release/CLAP/`, and the app to `.../Release/Standalone/`. Add `-DLODESTONE_BUILD_CLAP=OFF` to skip CLAP. Builds never copy into system folders on their own.

To build the installer locally, install [Inno Setup 6](https://jrsoftware.org/isinfo.php), then from the repository root:

```
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" /DAppVersion=2.0.0 /DBuildDir=..\build installer\Lodestone.iss
```

The installer is written to `installer\Output\`. To publish a release, either push a tag such as `v2.0.0` (it must match the version in `CMakeLists.txt`), or push the commit to the `release` branch: CI builds and verifies it, then creates the `v<version>` tag and the GitHub Release itself (it refuses a version that is already released).

### Verification

The **Build** workflow runs on pushes to `dev` and `release`, on version tags and on demand (not on `main`, to save Actions minutes: `main` only receives commits that already passed). A run only goes green if every check passes:

1. **DSP tests** (`lodestone_tests`): pitch detection within 2 cents from 82 Hz to 1.3 kHz with no octave errors, octave leaps followed in every Input range and weak fundamentals holding their octave, legato leaps landing within 32 ms (Studio) / 45 ms (Live) with no wrong note on the way, chords named at 44.1, 48 and 96 kHz within 0.35 s and never from drums alone, Chord Lock and chord harmonies landing on the right notes, Motion stepping through the scale and restarting with each phrase, no clicks when a glide crosses "no change", off-key notes land within 3 cents in both latency modes, in-tune audio passes through unchanged, transposed voices keep their formants (Modern) or move them (Classic), Throat moves the formants by the amount asked, scale-aware harmonies and MIDI modes land on the right notes, glides step through the scale, Stability ignores wide vibrato, Bounce overshoots and settles, and everything stays bounded on silence, noise, clipping input and every sample rate, at a few percent of one CPU core.
2. **State checks** (`lodestone_check state`): save and restore, byte-identical reloads, latency modes, end-to-end tuning through the plugin, LEARN on a real melody, custom notes, macros, undo, preset data and 25 random settings.
3. **Factory presets** (`lodestone_check presets`): every preset on a sung melody, as saved, with SNAP, with HOLD and with all four macros at full: no NaN, no clipping, no runaway level, no silence.
4. **Static and clicks** (`lodestone_check static`), on a held, sung note through the plugin: every factory preset, Dice, A/B, undo, redo and a host project load while singing (at 512- and 96-sample buffers, each fully settled within 64 ms), loop wraps with tempo-synced LFOs (512- and 1000-sample buffers), transport restarts over sound, every preset on a held note, and every knob jumped end to end and every switch flipped. No event may be more than 1.25 times as sharp as the voice itself (the same bar as Starfire and Possession).
5. **The beat** (`lodestone_check beat`): sidechain bus layouts (off, mono, stereo; others refused), Chord Lock end to end through the plugin (on and off), the chord shown and kept across a host reset, LEARN from the beat (A minor), no sidechain = the scale, Motion on the host's grid, and a 1.x project opening with Chord Lock 0 and Motion off.
6. **pluginval** at strictness 10, **Steinberg's VST3 validator** and **clap-validator**.

`tools/make_docs.py` regenerates `README.txt` and `docs/README.pdf` from this file.
