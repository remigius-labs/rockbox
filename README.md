![RPod: custom firmware for the iPod Classic, modernized Rockbox with a custom theme](https://raw.githubusercontent.com/remigius-labs/rpod/main/media/banner.png)

# RPod source

This is the `rpod` branch of a Rockbox fork: the full source of RPod, custom firmware for the iPod
Classic (6th/7th gen, Rockbox target ipod6g). Every change is its own commit on top of Rockbox
commit `59cc0282ec38919e22b7448d0f24c0713da4c21e`.

The download, the theme, install steps and the full list of changes are on the main page:
[github.com/remigius-labs/rpod](https://github.com/remigius-labs/rpod).

## Build

```sh
git clone -b rpod https://github.com/remigius-labs/rockbox.git
cd rockbox
# ARM toolchain: tools/rockboxdev.sh, then:
mkdir build-ipod6g && cd build-ipod6g
../tools/configure --target=29 --type=n
make -j8 && make zip
```

Rockbox's own readme is in `README` and `docs/`. Rockbox is GPL-2.0, and so are these changes.
