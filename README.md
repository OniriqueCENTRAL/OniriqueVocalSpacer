# ONIRIQUE VOCAL SPACER (JUCE 8 / VST3)

Adapte un instrumental terminé pour laisser de la place à la voix. Même DA que ONIRIQUE VOCALBOX.

Chaîne : INPUT -> SPACE -> VOCAL POCKET -> RESTORE -> AUTO GAIN -> OUTPUT (+ BYPASS). Latence nulle.
Chaque module (SPACE, VOCAL POCKET, RESTORE) a son bouton power (coupure sans clic).

## Modules
- **SPACE** (Mid / Side)
  - WIDTH 0..200 % : largeur stéréo (100 % = neutre).
  - LOW FOCUS : ramène les graves (< ~160 Hz) au centre, en mono.
  - SIDE DETAIL : brillance sur les côtés seulement (shelf jusqu'à +6 dB dès 3 kHz sur le Side).
- **VOCAL POCKET** : EQ dynamique 20 bandes (100 Hz - 12 kHz) pilotée par la voix envoyée sur l'entrée **sidechain**.
  Une bande n'est creusée que si la voix y est présente ET que l'instrumental la masque, et seulement pendant que la voix joue.
  - SIDECHAIN / STATIC : en STATIC (sans sidechain), poche fixe en forme de spectre vocal moyen (corps ~330 Hz, présence ~2.5 kHz).
  - MID / STEREO : creuse le centre seulement (défaut) ou toute l'image.
  - FAST / NORMAL / SMOOTH : ouverture 15 / 30 / 60 ms, maintien entre les syllabes 120 / 250 / 450 ms,
    fermeture 250 / 500 / 900 ms. La poche suit les phrases, pas les syllabes ni la batterie : pas de pompage.
  - DEPTH (0 à -12 dB), SENSITIVITY, RANGE LO / HI (aussi réglables à la souris dans l'afficheur).
  - LISTEN : n'écouter que ce que la poche retire.
  - Afficheur : spectres instrumental entrée / sortie, voix (rose), courbe exacte de la poche.
- **RESTORE**
  - KICK FOCUS : renforce uniquement les attaques du kick (62 Hz + claquement 3.2 kHz), jusqu'à +7 dB.
  - MID ENERGY : compression parallèle 400 Hz - 3.5 kHz ; au centre, elle recule quand la poche travaille.
- INPUT / OUTPUT (+/-24 dB, mètres crête), AUTO GAIN (loudness constante avant / après ; ignore la poche, pour ne
  jamais remonter la prod quand la voix entre), BYPASS (fondu, aussi piloté par le DAW).
- Presets usine + utilisateur (`%APPDATA%/Onirique/VocalSpacer/Presets/*.ovspreset`).

## Utilisation du sidechain
Insérer le plugin sur la piste (ou le bus) de l'instrumental, puis envoyer la piste voix vers l'entrée sidechain du plugin
(Ableton : menu "Sidechain" du plugin ; FL Studio : router la piste voix vers l'insert du plugin puis choisir l'entrée
dans "Processing" ; Reaper : canaux 3/4 ; Cubase / Studio One : bouton sidechain). Sans sidechain, passer en STATIC.

## Générer le .vst3 (Windows)
Prérequis : Visual Studio Build Tools 2022 (charge C++), CMake, Git.

    build_windows.bat

Le script réutilise les sources JUCE de `..\OniriqueVocalBox\build\_deps\juce-src` si elles existent (sinon JUCE 8.0.6 est téléchargé).

Résultat : `build\OniriqueVocalSpacer_artefacts\Release\VST3\ONIRIQUE VOCAL SPACER.vst3`
Copier ce dossier dans `C:\Program Files\Common Files\VST3` puis rescanner les plugins dans le DAW.

## Structure
- Source/Parameters.h       : IDs + layout APVTS
- Source/PluginProcessor.*  : bus principal + bus sidechain, paramètres, état, prises d'analyse
- Source/PluginEditor.*     : interface redimensionnable (DA VOCALBOX), afficheurs POCKET / SPACE / RESTORE
- Source/PresetManager.h    : presets usine + utilisateur
- Source/DSP/SpacerChain.h  : chaîne complète, interrupteurs de module, LISTEN, bypass, sécurité NaN/Inf
- Source/DSP/SpaceModule.h  : WIDTH / LOW FOCUS / SIDE DETAIL
- Source/DSP/VocalPocket.h  : analyse instrumental / voix, masquage, 20 cloches dynamiques, mode STATIC
- Source/DSP/RestoreModule.h: KICK FOCUS / MID ENERGY
- Source/DSP/AutoGain.h, Biquad.h, Common.h, SpectrumTap.h : repris de VOCALBOX
