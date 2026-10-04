# Website source

This is a framework-free static site for GitHub Pages. HTML is rendered at
build time; JavaScript only enhances the screenshot viewer. Downloads and
keyboard instructions work without JavaScript. Fonts are local, and reduced
motion, visible keyboard focus and narrow screens are supported.

From the repository root:

```sh
# Reproducible local preview, pinned to the verified alpha.8 release metadata.
python3 tools/build_site.py --snapshot site/release-snapshot.json
python3 -m http.server 8765 --bind 127.0.0.1 --directory build/site
```

Open `http://127.0.0.1:8765/`. The same relative paths work under the default
project URL, `https://psspssr.github.io/emerald-dual-screen-site/`.
This directory is source, not the deployment artifact; publish **build/site**.

## Fresh release links

For a production build, provide `GH_TOKEN` or `GITHUB_TOKEN` through the build
environment with repository **Contents: read** access, then run:

```sh
python3 tools/build_site.py
python3 -m unittest discover -s tools/tests -p test_site.py -v
```

The builder reads repository visibility and paginates the release list. It
selects the newest `published_at` with a safe version tag and all three fully
uploaded assets: the matching ARMv7 APK, `SHA256SUMS`, and `build-info.json`.
Drafts and incomplete uploads are skipped; **prereleases are included**.
It never uses `/releases/latest` or asks a public browser to access the private
GitHub API. The offline snapshot is an explicit fallback, not a live lookup.

Only allowlisted tag/date/download-link metadata is written to `release.json`.
API bodies, author/account data, credentials, game packs and APKs are not copied.
Image files are copied byte-for-byte from the existing project assets; the
site distinguishes real gameplay captures from the illustrative Thor mockup.

The game repository remains private. The deployment target is the separate
public **website-only** repository `psspssr/emerald-dual-screen-site`.
Publish only generated `build/site` files there, using a deploy key scoped to
that website repository; never copy the game checkout, APKs or credentials.
The publisher builds within the private game repository with Contents: read
access, then transfers the static output. It does not change game visibility.

The page states that GitHub repository access is required for downloads.
Publishing the website does not make private release assets public. This builder
does not enable hosting, push branches or publish releases.

Rebuild the site **after** the signed-release workflow has finished uploading
all assets; a release-published event can happen before those assets exist.
Keep the previous complete release linked while a new upload is incomplete.
The [Website workflow](../.github/workflows/website.yml) rebuilds on website
changes on `dev`, manual dispatch, and successful completion of **Android
release**. It reads release metadata with the game's read-only Actions token,
then uses `WEBSITE_DEPLOY_KEY`, scoped only to the public website repository,
to push the generated files. The source workflow also lives on default `main`
so GitHub can deliver release-completion events. App development stays on `dev`.

The publisher validates the complete generated file manifest before loading
credentials. Only the named HTML/CSS/JS, release metadata, screenshots and
three licensed font files are accepted. No directory-wide font copy, symbolic
links, APKs, saves or game-source checkout are published. Public Pages builds
from the website repository's `main` branch with HTTPS and no custom domain.

Pixelify Sans is bundled under SIL OFL 1.1; its licence and pinned source are in
`assets/fonts/`. The game and screenshot rights remain with their original
owners as described on the credits page and in the repository notices.
