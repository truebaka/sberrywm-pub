# SberryWM

> A weird experimental Wayland compositor built around an infinite desktop instead of traditional workspaces.

## What is this?

SberryWM is an experiment inspired by the ideas behind vxwm and MangoWC.

Instead of treating workspaces as isolated desktops, SberryWM organizes the desktop into three conceptual levels:

- **Basket** — the normal tiling area where windows are arranged.
- **Meadow** — an infinite canvas containing a group of windows. You can think of it as a workspace with actual spatial coordinates.
- **World** — a global overview showing every meadow at once through a movable camera.

The compositor is built around this model. Windows exist in world space, not just inside numbered workspaces.

## Features

- Wayland compositor based on wlroots
- Infinite canvas workspace model
- Camera-based global overview
- Tiling layouts
- Floating windows
- Workspace animations
- Overview mode inspired by MangoWC
- Simple runtime files for status bars and shells

## Project status

This project is effectively **finished** from my side.

I've released it into the wild.

- I will probably **not** respond to issues.
- Feature requests will almost certainly be ignored.
- If you fix something, open a Pull Request.
- If you want to continue the project, fork it.

## A warning

This project is:

- heavily vibe-coded;
- mostly contained inside a single gigantic `main.c`;
- surprisingly functional.

I genuinely have no idea why everything still works, but it does.

Good luck.

So if u want build it, configure `config.h` pls

## Inspiration

- vxwm(https://codeberg.org/wh1tepearl/vxwm)
- MangoWC(https://github.com/mangowm/mango)
