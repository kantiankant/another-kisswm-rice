# another-kisswm-rice

A minimal X11 desktop. Does its job. Doesn't make a song and dance about it.

![screenshot](/assets/2026-03-30_17-59-38.png)

---

## Components

| Thing | What it is |
|---|---|
| WM | [kisswm](https://github.com/slendr/kisswm) |
| Bar | kantbar (homemade, don't ask) |
| Terminal | [st](https://st.suckless.org) with alpha + scrollback patches |
| Notifications / OSD | [dunst](https://dunst-project.org) |
| Compositor | [picom](https://github.com/yshui/picom) |
| Fetch | [fastfetch](https://github.com/fastfetch-cli/fastfetch) |
| Editor | [neovim](https://neovim.io) |
| Font | SF Pro (Apple's, yes, don't start) |

---

## Dependencies

```
x11-libs/libx11
x11-libs/libxinerama
x11-libs/libxft
media-libs/freetype
x11-misc/picom
app-misc/fastfetch
```

---

## Installation

Clone it:

```bash
git clone https://github.com/kantiankant/another-kisswm-rice
cd another-kisswm-rice
```

Build the three things that need building:

```bash
cd kisswm && make && doas make install
cd ../kantbar && make && doas make install
cd ../st && make && doas make install
```

Copy the configs:

```bash
cp -r .config/* ~/.config/
```

That's it. No install script. No setup wizard. No guided onboarding experience. You're on Gentoo. You'll figure it out.

---

## Notes

- SF Pro is Apple's proprietary font and obtaining it is your own problem. You'll know where to look.
- Tested on Gentoo. Probably works elsewhere just fine.

---

## License

Do what you like with it. Credit would be nice but I'm not holding my breath.
