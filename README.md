# sehn - Simple, Minimal Camera Viewer

**sehn** is a light-weight, configurable V4L2 camera viewer and capture tool for X11.
It is inspired by [feh](https://feh.finalrewind.org/) and follows the same philosophy:
no toolkits, no bloat, do one thing well.

sehn opens a frameless window showing a live viewfinder feed from a V4L2 camera
device. A minimal panel provides capture, mode switching, and a settings menu.
Everything is configurable via TOML config files and command-line options.

Features include photo capture, video recording, burst mode, audio capture,
customizable keybindings, theme support, and EXIF metadata injection.

## Dependencies

 * libX11, libXext, libXft
 * cairo
 * libjpeg, libpng
 * libv4l2
 * libavcodec, libavformat, libavutil, libswscale, libswresample (FFmpeg)
 * libpulse, libpulse-simple
 * tomlc99 (bundled subproject fallback)

Optional:

 * libexif (EXIF metadata)
 * libcurl (MJPEG network streams - not yet implemented)

## Build Process

sehn uses the Meson build system.

```bash
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

The default prefix is `/usr`, so the desktop file, icon, and fonts are installed
to the correct system paths. Use `--prefix=/usr/local` or omit it for a local
install.

### Build Options

Compile-time optional features are reported in the version output:

```bash
sehn --version
```

## Configuration

sehn reads its configuration from `$XDG_CONFIG_HOME/sehn/sehnrc.toml`
(defaulting to `~/.config/sehn/sehnrc.toml`).

Additional config files:

 * `~/.config/sehn/themes.toml` - Named theme presets
 * `~/.config/sehn/keys.toml` - Custom key bindings
 * `~/.config/sehn/buttons.toml` - Mouse button bindings

Generate a config file from current settings:

```bash
sehn --print-config > ~/.config/sehn/sehnrc.toml
```

## Usage

```bash
sehn                          # Open default camera (/dev/video0)
sehn --list-devices           # List all V4L2 cameras
sehn -d /dev/video1 -r 1280x720 -R 60   # Specific device/resolution/FPS
sehn -F --no-panel            # Fullscreen, keyboard-only
sehn -m video --video-format mp4        # Start in video mode
sehn -T studio                # Load a theme
```

See the sehn(1) manual page for full documentation:

```bash
man sehn
```

## Contributing

Bug reports and feature requests are welcome via GitHub issues.

## License

MIT License. See source distribution for full text.

sehn is inspired by feh by Tom Gilbert and Birte Friesel.
