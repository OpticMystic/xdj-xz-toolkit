# XZ Mods loading artwork

`xz-mods.png` is the original artwork supplied by the project owner on
2026-09-24. Preserve its aspect ratio and embedded `vj.tools/xzmods` URL.

To prepare a private GUI pack from a locally obtained XZ 1.26 image pack:

```powershell
python mods/ui/build_branding.py --stock-pack <local-imagedata.dat> --output <new-imagedata.dat> --preview-dir <previews>
```

The command changes only loading images 1446 and 1487, verifies that all other
bytes remain unchanged, and renders previews decoded from the RGB565 pack.
The resulting pack contains locally supplied firmware graphics and must not
be distributed. `mods/private_usb.py --gui-pack <new-imagedata.dat>` includes
it in a private image whose loader binds the pack from RAM. Reboot clears the
RAM bind. This does not update a USB image already on the device.
