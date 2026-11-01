# G00 image assets

Build the `g00` CLI with the project's existing dependencies. It does not need
SDL initialization, a display, or additional Python packages:

```sh
cmake --preset debug
cmake --build --preset debug --target g00 -j 3
build/debug/g00 --help
```

The tool reads G00 types 0, 1, 2, and 3 through RLVM's image decoder. It writes
lossless type 2 files with BGRA pixels, alpha, and indexed sprite regions.
Conversion preserves decoded engine behavior rather than the source compression,
palette, JPEG encoding, chip partitioning, or linked-cut storage.

## Inspect and edit

```sh
build/debug/g00 info examples/game_root/image/sys_sel_btn01.g00
build/debug/g00 info examples/game_root/image/sys_sel_btn01.g00 --json

build/debug/g00 decode examples/game_root/image/sys_sel_btn01.g00 /tmp/button.png
# Edit /tmp/button.png with an image editor, keeping the canvas size.
build/debug/g00 encode /tmp/button.png /tmp/button.g00 \
  --metadata /tmp/button.png.json
```

`decode` writes the complete engine atlas as PNG and a JSON sidecar named
`OUTPUT.png.json`. Use `--metadata PATH` to choose a different sidecar location.
`encode` reads a sidecar only when explicitly given `--metadata`. Without a layout
option it creates one full-image cut. PNG inputs retain their exact dimensions
and straight alpha; resize or otherwise edit them before encoding.

To replace the pixels of an existing sheet while preserving its region indexes,
origins, and crops, use `--like` with a PNG matching the template's decoded atlas:

```sh
build/debug/g00 encode /tmp/button.png /tmp/button.g00 \
  --like examples/game_root/image/sys_sel_btn01.g00 --force
```

Existing output files require `--force`. PNG inputs and JSON metadata inputs
cannot be overwritten, even with `--force`. A `--like` template can also be the
destination: its layout is read before replacement. A decode checks both
destinations before writing, and each output is
written to a temporary sibling file before rename. Multi-file output is not a
single filesystem transaction.

## Create sprite sheets and extract cuts

```sh
# Four 444x59 button states in one 444x236 PNG, indexed from top to bottom.
build/debug/g00 encode /tmp/btn.png /tmp/buttons.g00 --grid 444x59

# A background gets one full-image cut.
build/debug/g00 encode /tmp/bg.png /tmp/background.g00

# Numbered PNGs: 0000.png, 0001.png, ... in region index order.
build/debug/g00 extract examples/game_root/image/sys_sel_btn01.g00 /tmp/buttons
```

`--grid WIDTHxHEIGHT` specifies cell dimensions, not the number of cells.
The image must divide into whole cells. Indexes run left to right, then top to
bottom. `--grid`, `--metadata`, and `--like` are mutually exclusive.

Extraction creates the destination directory and preserves each rendering
region's logical dimensions, including transparent padding around its pixel
crop. It does not apply origins as a translation or generate a packing manifest;
use the full PNG and JSON workflow to preserve origins during editing.

## Metadata

JSON metadata is versioned. Rectangles use `[x, y, width, height]`, with x/y in
the decoded PNG atlas, and origins use `[x, y]`. For example:

```json
{
  "version": 1,
  "width": 444,
  "height": 236,
  "regions": [
    {
      "rect": [0, 0, 444, 59],
      "origin": [0, 0],
      "pixels": {"rect": [0, 0, 444, 59], "origin": [0, 0]}
    },
    {
      "rect": [0, 59, 444, 59],
      "origin": [0, 0],
      "pixels": {"rect": [0, 59, 444, 59], "origin": [0, 0]}
    }
  ]
}
```

`rect` and `origin` describe rendering. Optional `pixels` describes the stored
crop and its sampling origin relative to that crop. Omitting `pixels` stores the
entire rendering rectangle with sampling origin zero. An empty cut has a pixel
rectangle `[region_x, region_y, 0, 0]` and sampling origin `[0, 0]`. An empty
`regions` array encodes as one full-image cut.

Pixel crops must lie within their rendering rectangles, which must lie within
the atlas. Only pixels inside the specified crops are written; edits in logical
padding require expanding the crop metadata. Original stacked cuts are exported
as distinct atlas cells and re-encoded in that layout. Keep those cells separate:
identical rendering rectangles would activate the engine's automatic stacking.

Dimensions are limited to 32767 per axis, region counts to 32767, and file,
pixel-buffer, and uncompressed payload sizes to 256 MiB. Invalid sizes, cut
offsets, chip bounds, truncated data, and invalid LZ references produce an error.
Commands return 0 on success and 1 on errors, with diagnostics on stderr.

