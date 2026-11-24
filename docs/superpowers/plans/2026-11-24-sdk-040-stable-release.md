# SDK 0.4.0 stable release execution

User authorization: merge the current independent-runtime work into main, upload
to GitHub as a formal release, and provide a Hurdle King upgrade prompt. This
supersedes the earlier local-only publication restriction. Preserve caches,
existing worktrees and business projects; no automatic business-game upgrade.

1. Fetch origin, verify main ancestry and the absence of v0.4.0. Preserve main's
   four earlier proposal edits with exact hashes and a scoped Git stash before
   the final fast-forward. Never force-push or overwrite a published tag.
2. Prepare SDK 0.4.0 / Input 0.1.0 together. Update the managed version and active
   installation/API/release documents. Preserve old ABI and model eligibility
   markers. Runtime and native source stay those of the verified candidate.
3. Stage qualified Windows SDK/Input and Android Input. Build public Android SDK
   from the same source with RKNN and private experiments explicitly disabled;
   audit actual API26 imports, exports and dependency closure. Keep the complete
   private RKNN/vendor/model addon local with a separate matched hash manifest.
4. Refresh package asset indices and generate release-040-authority.json only
   after package docs and native bytes are frozen. Run native regression,
   architecture/catalog/Input isolation/newline checks and package tests.
5. Validate actual Input-first offline and UPM imports in new isolated Unity
   projects, including real Windows model initialization and relevant legacy
   fixture diagnosis. Preserve full XML; fix real failures before publication.
6. Review the Hurdle King prompt against the actual preview.17 integration,
   using the new tag and exact public/local package identities. Do not edit or
   publish the game/AAR while producing this prompt.
7. Commit the verified release tree, fast-forward main without discarding local
   edits, run final snapshot guards on the merged source, and build the eight
   public artifacts from that exact commit. Keep local addons and test APKs out
   of GitHub attachments.
8. Push main and an annotated v0.4.0 tag. Create a draft Release, upload the
   eight public files, download into a separate directory, compare every hash,
   then publish with prerelease=false and latest=true. Verify remote ref,
   tag target, release metadata and asset hashes. Report the release URL and
   provide the ready-to-use upgrade prompt.

Acceptance scope: Windows/Android public packaging and the reported functional
checks. RK3588/RGA, long thermal or game GPU pressure, physical camera/file native
drivers, complete hand endpoints and per-person 30 FPS remain unverified.
