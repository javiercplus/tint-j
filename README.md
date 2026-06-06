# New stable release: 0.12.6 (tint-j fork)
Changes: https://gitlab.com/o9000/tint2/blob/0.12.6/ChangeLog

Documentation: https://gitlab.com/o9000/tint2/wikis/Configure

## tint-j Fork

This is the **tint-j** fork of tint2, which includes experimental **Wayland support** through a separate binary called `tint-wl`.

### Building with Wayland Support

To build both the X11 (tint2) and Wayland (tint-wl) binaries:

```
git clone https://github.com/javiercplus/tint-j.git
cd tint-j
mkdir build
cd build
cmake ..
make -j4
```

This will produce two binaries:
- `./tint2` / `./src/tint2conf/tint2conf` - Traditional X11 panel
- `./tint-wl` - Wayland-native panel (experimental)

To install from source, also run (as root):
```
make install
```

### Running

**For X11:**
```
./tint2 &
./src/tint2conf/tint2conf &
```

**For Wayland:**
```
./tint-wl &
```

Please report any problems to https://github.com/javiercplus/tint-j/issues. Your feedback is much appreciated.

P.S. GitLab is now the official location of the tint2 project, migrated from Google Code, which is shutting down. In case you are wondering why not GitHub, BitBucket etc., we chose GitLab because it is open source, it is mature and works well, looks cool and has a very nice team.

# What is tint2?

tint2 is a simple panel/taskbar made for modern X window managers. It was specifically made for Openbox but it should also work with other window managers (GNOME, KDE, XFCE etc.). It is based on ttm http://code.google.com/p/ttm/.

# Features

  * Panel with taskbar, system tray, clock and launcher icons;
  * Easy to customize: color/transparency on fonts, icons, borders and backgrounds;
  * Pager like capability: move tasks between workspaces (virtual desktops), switch between workspaces;
  * Multi-monitor capability: create one panel per monitor, showing only the tasks from the current monitor;
  * Customizable mouse events.

# Goals

  * Be unintrusive and light (in terms of memory, CPU and aesthetic);
  * Follow the freedesktop.org specifications;
  * Make certain workflows, such as multi-desktop and multi-monitor, easy to use.

# I want it!

  * [Install tint2](https://gitlab.com/o9000/tint2/wikis/Install)

# How do I ...

  * [Install](https://gitlab.com/o9000/tint2/wikis/Install)
  * [Configure](https://gitlab.com/o9000/tint2/wikis/Configure)
  * [Add applet not supported by tint2](https://gitlab.com/o9000/tint2/wikis/ThirdPartyApplets)
  * [Other frequently asked questions](https://gitlab.com/o9000/tint2/wikis/FAQ)
  * [Obtain a stack trace when tint2 crashes](https://gitlab.com/o9000/tint2/wikis/Debug)

# How can I help out?

  * Report bugs and ask questions on the [issue tracker](https://github.com/javiercplus/tint-j/issues);
  * Contribute to the development by helping us fix bugs and suggesting new features.

# Links
  * Home page: https://github.com/javiercplus/tint-j
  * Git repository: https://github.com/javiercplus/tint-j.git
  * Documentation: https://gitlab.com/o9000/tint2/wikis/home
  * Downloads: https://github.com/javiercplus/tint-j/releases
  * Old project location (inactive): https://code.google.com/p/tint2

# Screenshots

## Default config of the latest release:

![Screenshot_2016-01-23_14-42-57](https://gitlab.com/o9000/tint2/uploads/948fa74eca60864352a033580350b4c3/Screenshot_2016-01-23_14-42-57.png)

## Various configurations:

![screenshot](https://gitlab.com/o9000/tint2/wikis/screenshot.png)
