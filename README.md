# Eclipse Lun3r and Eclipse Hydra web browsers

[Official Discord Server](https://discord.gg/ecx)

Eclipse Lun3r is a fork of roytam1's New Moon which is a fork Pale Moon made for Windows XP.

![big1](https://github.com/user-attachments/assets/3e3d2bff-d97c-4dc4-a471-913ddcf983e5)

Eclipse Hydra is a fork of roytam1's Serpent which is a fork of Basilisk made for Windows XP.

![big1](https://github.com/user-attachments/assets/0bd7d7df-0d9a-46b3-9273-2ba7cae5fbdb)

Some advantages over upstream and roytam1's New Moon are:
- More optimizations than upstream.
- Installer and true portable variants.
- Features from Mypal like duplicate tab.
- Experimental e10s support. (it works better in Hydra)
- Better interoperability with upstream. (using SQL storage intead of DBM)
- JPEG XL support!
- Lun3r/New Moon theme coloring is fixed on XP.
- Windows theme version override.
- Added back Nvidia 3D. (untested)
- Integrated PDF reader.

## Credits

If I've forgotten to put your name here, please let me know and I'll add it.

- [Feodor2](https://github.com/Feodor2) - Portable mode, theme fixes, and more from Mypal.
- [Librewolf Developers](https://codeberg.org/librewolf) - Privacy tweaks from Librewolf. (Has not been implemented yet)
- [Solinus](https://solinus.neocities.org/) - Branding visuals, icons and fancy text.

Major core browser engine contributions:
- [wuggy (Dactyloidae)](https://repo.dactyloidae.xyz/Dactyloidae) - Multiple improvements to the browser, most notably the big performance improvements. (Has not been implemented yet)
- [roytam1](https://github.com/roytam1) - Windows XP support code, e10s, WebExtensions, and more for UXP.
- [Moonchild Productions](https://repo.palemoon.org/MoonchildProductions) - For creating the Unified XUL Platform, the core of this project.
- [Mozilla Developers](https://github.com/mozilla-firefox) - Firefox ESR 52 browser base.

# Original repository readme

# Unified XUL Platform (UXP)

This repository holds the code for a unified application platform for XUL-based
applications. It is a hard fork from the Mozilla code repository (mozilla-central)
with an ESR-52 fork point.

In addition to further development based on the Mozilla upstream code, and
selective cherry-picking of directly-applicable patches, this repository has its
own development and holds the base for a maintained platform to be used by XUL
applications.

For a list of active projects making use of the Unified XUL Platform, checkout http://thereisonlyxul.org/.

## Additional documentation

Additional documentation relevant to this source code can be found in the `/docs`
directory. This will contain relevant documentation regarding contributing,
using and distributing this code and its binaries.

If you are interested in the development and building side of things, some
information will be available on the [Pale Moon developer site](http://developer.palemoon.org).
You are also always welcome to get in touch with our community on the [Pale Moon forum](https://forum.palemoon.org/).

### A note about trademarks and branding

Although this repository is licensed under Mozilla Public License v2.0, the
trademarks and brands contained herein remain the property of their respective
owners. For more details, please see the notifications in the respective directories.

### Foundation and maintainership

This repository has been founded by Moonchild (M.C. Straver) and is maintained by him
and other community members.
If you fork this repository to perform your own work on it, please consider offering
improvement patches upstream to its origin to mutually improve the platform and build
a future for XUL.
