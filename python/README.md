# tacozip

`tacozip` is a fast, minimal ZIP64-like archiver with **ghost metadata** support for AI4EO workflows.

## Example

```python
import tacozip
from pathlib import Path

# Path to folder with files to add
src_dir = Path("/content/sample_data")
zip_out = Path("my_archive.taco.zip")  # Output archive file name

# Collect all files (non-recursive)
src_files = [str(f) for f in src_dir.iterdir() if f.is_file()]
arc_files = [f.name for f in src_dir.iterdir() if f.is_file()]  # Inside-archive names

# Create archive
rc = tacozip.create(
    str(zip_out),
    src_files,
    arc_files,
    meta_offset=0,    # Ghost metadata offset
    meta_length=0     # Ghost metadata length
)
print(f"tacozip.create() returned {rc}")
print(f"Created archive: {zip_out.resolve()}")

# Read ghost metadata
rc, offset, length = tacozip.read_ghost(str(zip_out))
print(f"read_ghost() rc={rc}, offset={offset}, length={length}")

# Update ghost metadata in-place
rc2 = tacozip.update_ghost(str(zip_out), new_offset=1234, new_length=5678)
print(f"update_ghost() rc={rc2}")

# Read again to confirm
rc3, offset2, length2 = tacozip.read_ghost(str(zip_out))
print(f"Updated ghost: offset={offset2}, length={length2}")
```

## Install

```bash
pip install tacozip
```
