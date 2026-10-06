# GD Evolve

Editor pause menu -> **Evolve Image** -> pick a PNG/JPG. The mod draws every
object ID's real sprite in-game, then evolves objects (ID, hue, saturation,
brightness, size, rotation, position, z order) until they match the image,
and pastes them into the editor. The raw object string is also saved to
`<mod save dir>/last_evolve_objects.txt`.

Build: push this folder to a GitHub repo, the Actions workflow builds it and
gives you a zip with the `.geode` file.

If it will not load: set the `gd` versions in mod.json to your GD version.
If it will not compile: send the compiler errors back, the Geode-API calls
(`intKeyToFrame`, `pasteObjects`, `file::pick`) were written without a compiler.

Offline engine test: `g++ -O2 -std=c++17 tests/test_engine.cpp -o te`
