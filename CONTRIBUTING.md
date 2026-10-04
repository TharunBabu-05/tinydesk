# Contributing to TinyDesk

Bug reports and patches are welcome. Include the board, terminal and source
revision when reporting a problem.

1. Clone with `git clone --recursive` (the shell is the submodule
   `third_party/tdsh`, repository `tinydesk-shell`).
2. Build and test on the PC: `cmake -B build -G Ninja && cmake --build build && ctest --test-dir build`.
3. Build the firmware you touched: `idf.py build` in `ports/esp32c6`, `ports/esp32` or `ports/esp32-4mb`
   (ESP-IDF 5.3.1). Code in `ports/esp_idf` is shared by all three.
4. Describe what changed for users (and the API) in the pull request, so the documentation
   and the changelog can follow.
5. Format the C files you change with clang-format 16: `clang-format -i` with the repository's `.clang-format`
   (one way to get it: `pip install clang-format==16.0.6`).
6. Keep your own board out of it: pins go in `ports/*/board.conf` (ignored by git),
   never in code. New hardware keys go, commented out, into both `board.example.conf` files.

Shell changes are made and pushed in `tinydesk-shell` first; then the new
submodule commit is recorded here.

Security problems: report them privately via GitHub (Security → Report a vulnerability).
Contributions are released under the MIT licence.
