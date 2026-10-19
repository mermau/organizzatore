#!/bin/bash
# Installa Organizzatore (VST3, AU, Standalone). Richiede la password di amministratore.
cd "$(dirname "$0")"
set -e
echo "Installazione di Organizzatore…"
sudo mkdir -p /Library/Audio/Plug-Ins/VST3 /Library/Audio/Plug-Ins/Components
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/Organizzatore.vst3" "/Library/Audio/Plug-Ins/Components/Organizzatore.component" "/Applications/Organizzatore.app"
sudo cp -R "Organizzatore.vst3" /Library/Audio/Plug-Ins/VST3/
sudo cp -R "Organizzatore.component" /Library/Audio/Plug-Ins/Components/
sudo cp -R "Organizzatore.app" /Applications/
sudo xattr -dr com.apple.quarantine "/Library/Audio/Plug-Ins/VST3/Organizzatore.vst3" "/Library/Audio/Plug-Ins/Components/Organizzatore.component" "/Applications/Organizzatore.app" 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Fatto. Riapri la DAW per vedere Organizzatore tra gli strumenti (Collateral Effects)."
