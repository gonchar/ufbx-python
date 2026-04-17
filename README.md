# ufbx-python

Python bindings for the [ufbx](https://ufbx.github.io/) FBX loader.

## Install

Prebuilt wheels are attached to every release:
<https://github.com/gonchar/ufbx-python/releases>

```bash
# macOS arm64 (Python 3.12)
pip install --force-reinstall --no-cache-dir \
    https://github.com/gonchar/ufbx-python/releases/download/v0.0.5.post1/ufbx-0.0.5.post1-cp312-cp312-macosx_11_0_arm64.whl

# Linux x86_64 (Python 3.12)
pip install --force-reinstall --no-cache-dir \
    https://github.com/gonchar/ufbx-python/releases/download/v0.0.5.post1/ufbx-0.0.5.post1-cp312-cp312-manylinux_2_17_x86_64.manylinux2014_x86_64.whl
```

`--force-reinstall --no-cache-dir` matters — without both, pip may skip the
install if a previously cached `ufbx` is around.

**Source install** (for platforms without a prebuilt wheel):

```bash
pip install --force-reinstall --no-cache-dir \
    git+https://github.com/gonchar/ufbx-python.git@v0.0.5.post1
```

Source install compiles the C extension. Prerequisites:

- **macOS**: `xcode-select --install`
- **Debian/Ubuntu**: `apt-get install build-essential python3-dev`
- **Windows**: MSVC Build Tools
- Python 3.9+ (tested on 3.12)

## Usage

```python
import ufbx

scene = ufbx.load_file("path/to/model.fbx")
for node in scene.nodes:
    print(node.name, node.node_to_world)
```

Use Scene as a context manager for deterministic teardown:

```python
with ufbx.load_file("path/to/model.fbx") as scene:
    ...
```

## Releasing

Push a `v*` tag; the `Build + publish wheels` workflow
(`.github/workflows/publish.yml`) builds wheels for macOS arm64, macOS
x86_64, Linux x86_64, Linux arm64 across Python 3.11 / 3.12 / 3.13 via
`cibuildwheel` and attaches them to the GitHub Release.

```bash
git tag -a v0.0.5.post2 -m "description"
git push origin v0.0.5.post2
# wait ~5-10 min, then check the Releases page
```

## Maintenance

- **Owner:** gonchar
- **C extension source:** `ufbx/ufbx.c` + `ufbx/ufbx.h` live in-tree. The
  Python wrapper layer (`ufbx/prelude.h`, `ufbx/native.c`) and the
  generated binding (`ufbx/generated.h`) are regenerated from `ufbx.h`
  via `just parse && just generate`.
- **Regenerating bindings** after editing `ufbx/ufbx.h`:
  ```bash
  just parse && just generate
  ```
