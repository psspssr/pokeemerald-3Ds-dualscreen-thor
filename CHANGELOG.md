# Changelog

## 0.2.0 — 2026-10-04

New and improved:

- The game's own menus on the bottom screen, by touch: the party menu (with
  the summary's move screens), the bag, the Pokédex and the PC boxes are the
  original screens, built as a centred picture with the background carried
  to the edges; every action of the originals is reachable by tapping, and
  they run at 60 fps on Old 3DS.
- 3D BATTLE option (off by default, needs VOXEL 3D): battles are drawn in
  front of the voxel world, on level ground found near the player, with the
  camera gliding in from the field's.
- The FPS counter is now the SHOW FPS option of the bottom screen's OPTION
  list: off by default, turn it on there (it used to be always shown).
- Voxel (experimental, still full of errors and under active work): HD-2D look - tilt-shift blur at the top and bottom of the screen
  (3D BLUR option), bloom, sunlit dust, natural sunlight with richer colour,
  sun rays, soft sun dapples that stay on the ground across map crossings,
  lighter tree crowns and softer shadows.
- Voxel: mountains in their true tile shapes, with cut tiles, rock edges that
  follow the rock and flanks that rise across their drawn run; sea rocks and
  boulders modelled once wherever they repeat; Jagged Pass, Mt Chimney and
  Lavaridge read as mountains; Route 106's ledge apart from its plateau.
- Voxel: fog banks lying over the ground and thinning into the distance, and
  a cold, dim light with a dark ring in caves; in 2D the fog covers the whole
  screen and is see-through.
- Voxel: water lies flush with the ground (no line along the shores), tall
  grass rustles on its own tile, the surf mon, grass, "!" icons and splashes
  are drawn, and sprites stand in front of stairs.
- Much less stalling on the console: assets read ahead by a background
  worker, the sound mixer on another core, unchanged layers not walked, New
  3DS at 804 MHz with L2; the voxel frame scheduling, frustum view and chunk
  builds reworked for Old 3DS, draft chunks filling holes at once, animated
  tiles uploading only what changed.
- Builder about 2.5x faster (a full build takes about 6.5 minutes instead of
  15-17) with a byte-identical pack.
- Releases also carry `Emerald3DS.3dsx` and `Emerald3DS.smdh` on their own
  (quick update) and `Emerald3DS-WebPayload.zip` with `web-manifest.json` for
  the web builder; the builder can run the voxel generators in-process (used
  by the web builder), and the Windows ZIP's `LICENSES/` folder again
  includes `LICENSE-PORT.md`, `NOTICE.md` and `AI_DISCLOSURE.md`.
- Optional HOME Menu forwarder, `Emerald3DS-Forwarder.cia`: installed once
  with FBI, it starts the game's 3DSX through Luma3DS without needing the
  Homebrew Launcher title, so updates only replace the 3DSX. The game's own
  icon is the new project icon.
- The SD log is only written when `sdmc:/3ds/emerald3ds/debug.txt` exists;
  `port.log` holds the current session and `port-prev.log` the previous one.

Fixes:

- Voxel: the overworld crashed on the console as soon as the sun dapples
  drew (a null texture unbind that the emulator let pass).
- Voxel: freezes after battles, chunks that stopped loading near Mauville and
  the overworld falling back to 2D on Old 3DS; each option tap froze the game
  for about a second (settings are now saved by their own thread).
- Voxel: sun dapples jumped at every map crossing.
- Voxel: grass floated between tufts and walkers hopped in it.
- Voxel: black gaps at the edges of the view after a crossing.
- 2D: the Surf, Fly and field-move banner left the rest of the field black.
- 2D: Match Call turned the map to garbage while a call lasted.
- 2D: the map name popup peeked in at the bottom of the screen as it slid
  away.
- Items shown by the PC and the like fell outside their frame, and the
  healing machine's balls were not on the machine.
- Graphics that were read past their stubs and never showed: PC wallpapers,
  party menu and summary tilemaps, Deoxys's form icon.
- The starter choice screen was drawn at the top-left of the top screen with
  a teal backdrop beyond the meadow; it is now centred 1:1 with the meadow
  carried to every edge, and the left starter's label (Treecko) keeps its
  darkened box.
- The forwarder hung on a black screen.

## 0.1.2 — 2026-09-30

New and improved:

- PokéNav on the bottom screen, driven by touch: laid out as header, body
  and help bar, a tap moves the cursor straight to the option or entry, and
  the Hoenn map scrolls smoothly.
- 3D slider: the intro and the title screen now have stereo depth, and 2D
  screens keep blended sprites and text windows at their own depth.
- Battle transitions now play in both the classic 2D field and the voxel
  overworld, at full speed on Old 3DS.
- Voxel: the world fades with the screen on warps and battle starts.
- Voxel: characters are drawn at their proper proportions, stand on their
  feet and meet their shadow; the walking bob is kept.
- Voxel: walls and mountain sides facing away from the sun are in shade,
  lit by the same sun as the cast shadows.
- Voxel: newly modelled Rustboro's gym (its statues stand in all eight gyms),
  Oldale's two houses, Mr Briney's cottage and the Pretty Petal flower shop.
- Voxel: every mountain of the general tileset is drawn; Route 116's
  mountain is built from straight faces and one-level steps; stairs, lamps
  and Rustboro's railings stand again.
- Voxel: Route 104's beach and sea are a level below the route, and Routes
  105 and 106 follow them down.
- Intro, title and menus: the margins around the 240x160 picture are made
  from each screen's own art (Rayquaza stands on the bottom edge under a lit
  sky, the Professor's sky reaches the top), and menu screens are centred.
- Title screen and palette fades much smoother on Old 3DS: layer textures
  are redrawn only where they change, and fades are drawn as a tint.
- Installation guide in `docs/INSTALLATION.md`, and bug report and feature
  request forms on GitHub.

Fixes:

- PokéNav tiles and palettes were wrong.
- With the 3D slider up, black screens came out blue and the save selection
  lost its backdrop; the intro's leaves scene slowed down.
- Voxel: sprites went black on their own during warps and battle starts.
- Voxel: cast shadows striped and came apart from the feet.
- Voxel: black gaps along the shores of recessed water (now a rim).
- Voxel: ledges were sunk into a toothed trench; they now sit on the ground
  as on Route 101, and side ledges are no longer striped by the lighting.
- Voxel: Rustboro's lamps against walls did not stand.

## 0.1.1 — 2026-09-27

- The voxel overworld is now off by default. It is switched on from the new
  **VOXEL 3D** row of the bottom-screen OPTION screen.
- While it is on, **3D ANGLE** (34-46 degrees, default 40) and **3D ZOOM**
  (90-120%, default 100%) adjust its camera.
- These settings are kept in `/3ds/emerald3ds/settings.txt` and apply at once,
  without saving the game.
- Classic 2D field: text boxes, prompts and the map name are centred as in
  the voxel view.
- Classic 2D field: much faster on Old 3DS; the backgrounds are kept in
  textures and only the cells that change are redrawn.
- Classic 2D field: sprites just below the view are no longer drawn at the
  top of the screen.

## 0.1 — 2026-09-26 — first public version

- Native ARM11 port of pokeemerald for Nintendo 3DS: GPU compositor at
  native 400x240, NDSP audio, touch and Circle Pad input, SD saves.
- Bottom-screen interface replacing the START menu (map, party, bag, trainer
  card, Pokédex, PokéNav, save, options) and touch battle menus.
- Optional voxel overworld with buildings, trees, signposts and terrain relief
  modelled from each map's own art, fixed-sun lighting and cast shadows.
  The modelled part is still limited: most of the map and nearly all
  interiors are shown flat for now.
- Game data outside the executable: embedded (development), loose files or a
  single `emerald3ds.pak` with ABI and integrity checks.
- Pokémon Emerald 3Ds Dual Screen Builder: generates the data pack from the player's own ROM and
  installs the game on an SD card, with no toolchain or Python required on
  Windows.
