# PNG Analyzer brand assets

The application icon is a deterministic vector reconstruction of the approved
`png_analyzer_icon.png` reference. It preserves the image frame, isolated pixel,
stepped pixel image, magnifier, and three inspected pixels. Production assets
use flat geometry, have a transparent exterior, and contain no shadows or
gradients.

## Palette

- Background: `#F8FAFC`
- Magnifier: `#2563EB`
- Lens: `#E2E8F0`
- Pixel 1: `#14B8A6`
- Pixel 2: `#F59E0B`
- Pixel 3: `#8B5CF6`
- Lockup label: `#475569`

## Deliverables

- `branding/png-analyzer-icon.svg`: vector app-icon master.
- `branding/png-analyzer-icon-reference.png`: original 1254 px design reference;
  not used at runtime because it has no alpha channel and contains black corners.
- `branding/png-analyzer-lockup.svg`: vector brand lockup for About/README use.
- `branding/png-analyzer-lockup.png`: transparent raster lockup.
- `icons/png/`: RGBA PNGs at 16, 24, 32, 48, 64, 128, 256, 512, and 1024 px.
- `packaging/icons/png-analyzer.ico`: Windows multi-resolution icon (16 through 256 px).
- `packaging/icons/png-analyzer.icns`: macOS icon family.
- `packaging/icons/linux/hicolor/`: freedesktop hicolor application icons.

The 16, 24, 32, 48, 64, and 128 px renderings were visually checked on a dark
background. All PNG corner pixels are fully transparent.
