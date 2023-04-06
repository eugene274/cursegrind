# cursegrind 0.0.1
Cursegrind - a lightweight curses-based callgrind output vizualizer

### Dependencies

- CMake 3.11 or newer
- gcc with std17
- libcurses
- Boost

### Main view
<img src="https://user-images.githubusercontent.com/23106384/146260382-931977ac-6b14-40a6-b27e-6e54307ad2ab.png" alt="screenshot of the main view" width="250px">

### Intallation

```
$ mkdir build
$ cmake -DCMAKE_INSTALL_PREFIX=<path-to-install-dir> ../
$ make && make install
```

### Usage

`$ cursegrind <path-to-callgrind-output-file>`

### Keybindings

- `left arrow, h` - collapse item
- `right arrow, l, e` - expand item
- `up arrow, k` - move up
- `down arrow, j` - move down
- `/` - activate search panel
- `c` - toggle costs view (Ir/Percentage from total)
- `v` - toggle symbol / filename::symbol / object::symbol representations
- `F10` or `q` - exit






