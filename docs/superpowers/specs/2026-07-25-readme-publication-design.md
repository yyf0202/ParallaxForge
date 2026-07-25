# README publication design

## Goal

Make the public README a compact entry point for ParallaxForge and publish the
current branch as the public `yyf0202/ParallaxForge` repository.

## README shape

The README will contain only:

1. A one-sentence description of PVS baking from OBJ geometry.
2. Three core capabilities: GPU voxel occupancy, free-voxel Probe generation,
   and six-face DXR visibility baking.
3. Windows/DX12/DXR prerequisites.
4. One compact Build section containing the conventional configure and build commands, plus one `--bake` command.
5. The JSON/PFVIS output names.

It will omit test, smoke, implementation, and historical detail.

## Publication

Create a public GitHub repository named `yyf0202/ParallaxForge`, push
`codex/parallax-forge-foundation`, and use that branch as the published
repository's default working branch. No pull request is needed because this is
the initial public repository.
