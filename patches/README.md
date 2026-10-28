# Local submodule patches

These are working-tree modifications made inside git submodules. Submodules point
at commits in upstream repositories this repo cannot push to, so the changes are
stored here as patch files instead of commits.

| Patch | Submodule | Summary |
|---|---|---|
| `sokol.patch` | `dependencies/sokol` | Handle unterminated `text/uri-list` drop lists; add ALSA `device_name` to `saudio_desc` |
| `protobuf-c.patch` | `dependencies/protobuf-c` | Zero the message buffer before `message_init` so stale heap bytes are never read as pointers |
| `imguifiledialog.patch` | `dependencies/imguifiledialog` | Return the `WasOpenedThisFrame` / `IsOpened` results instead of discarding them |

## Applying

```sh
git submodule update --init dependencies/sokol dependencies/protobuf-c dependencies/imguifiledialog
git -C dependencies/sokol            apply ../../patches/sokol.patch
git -C dependencies/protobuf-c       apply ../../patches/protobuf-c.patch
git -C dependencies/imguifiledialog  apply ../../patches/imguifiledialog.patch
```

## Regenerating

If the submodule working trees are modified again:

```sh
git -C dependencies/sokol           diff > patches/sokol.patch
git -C dependencies/protobuf-c      diff > patches/protobuf-c.patch
git -C dependencies/imguifiledialog diff > patches/imguifiledialog.patch
```

Note: `out/` directories inside submodules are build artifacts and are ignored on
purpose — they are not part of these patches.
