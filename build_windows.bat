@echo off
REM Nettoie puis compile ONIRIQUE VOCAL SPACER (Release). A lancer depuis ce dossier.
REM Si les sources JUCE de VocalBox sont deja la, on les reutilise (pas de telechargement).
set JUCE_LOCAL=
if exist "..\OniriqueVocalBox\build\_deps\juce-src\CMakeLists.txt" set JUCE_LOCAL=-DFETCHCONTENT_SOURCE_DIR_JUCE=%~dp0..\OniriqueVocalBox\build\_deps\juce-src
if exist build rmdir /s /q build
cmake -B build %JUCE_LOCAL% || goto :err
cmake --build build --config Release || goto :err
echo.
echo OK : build\OniriqueVocalSpacer_artefacts\Release\VST3\ONIRIQUE VOCAL SPACER.vst3
goto :eof
:err
echo.
echo ERREUR pendant la compilation, voir les messages ci-dessus.
exit /b 1
