# Third-party runtime dependencies

`third_party/onnxruntime/` is generated locally and ignored by Git.

The D0 native baseline is the official Microsoft ONNX Runtime 1.29.0 CPU Windows x64 archive:

- URL: `https://github.com/microsoft/onnxruntime/releases/download/v1.29.0/onnxruntime-win-x64-1.29.0.zip`
- Archive SHA-256: `c9b4b7086b529ad814f428c1bad028e20a25d7dc0699836775faace4ab5b78b2`
- Package `GIT_COMMIT_ID`: `2e2543fbe9fae542f921d47a72d21d5a4ef0b710`
- Runtime DLL SHA-256: `69d8e6d3879a3b4001cdc74c8ed9ccc7e7f799a5b847059738323404519ec471`

Install or verify it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/setup/download_onnxruntime.ps1
```

The DLL and import library are not committed. CMake fails with an actionable setup command when this dependency is absent.
