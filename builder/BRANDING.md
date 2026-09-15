# DJ branding bundle, version 1

XZ Mods Builder writes immutable bundles at
`<USB root>/VJ.Tools/Branding/bundles/<UUID>/manifest.json`.
Choosing a staging folder produces the same structure; copy its `VJ.Tools`
folder to the USB root. Existing bundles and music are never overwritten.
Each import creates a new UUID. There is no mutable latest pointer.

The UTF-8 JSON manifest has `schema: "vj.tools.dj-branding"`, integer
`version: 1`, `id`, `artist`, optional nullable `website`, ISO 8601 UTC
`created_at`, and `files`. Every file has `role` (`logo`, `epk`, or `media`),
`name` (display only), `path` (relative to its manifest directory), integer
`bytes`, and lowercase hexadecimal `sha256`. Absolute source paths are omitted.
Files are copied unchanged. The manifest is published with the complete bundle.
Hidden `.import-*` directories are incomplete and must not be discovered.

VJ.Tools and Wingman consumers are not implemented yet. Their discovery contract
is to list immediate UUID directories under this well-known path, accept only
supported schema versions, and show the artist and available roles before a VJ
chooses files. Multiple bundles must remain selectable; timestamps do not imply
that an earlier bundle may be deleted. Enumeration does not grant permission to
copy the rest of the USB.

A future network adapter should expose listing and read-only file retrieval
scoped to these bundles. It must validate remote manifests as untrusted input:
at most 64 files, 2 GiB per file and 4 GiB total; reject absolute paths, `..`,
backslashes, drive prefixes, links and paths escaping the manifest directory;
check sizes and SHA-256 after download. Never execute assets, automatically open
websites or render active document content. Preserve a local copy's provenance
with the bundle ID and digest. Use authenticated/approved device connections and
explicit file selection. No new network listener or unrestricted USB file server
is added by the builder.

The current import allowlist is PNG/JPEG/WebP for logos, PDF/plain text for EPKs,
and PNG/JPEG/WebP/MP4/MOV/WebM for visuals. Extensions classify files; they do not
prove decodability. Consumers still validate media before decoding. User assets
retain their original ownership and are never included in the XZ Mods installer.
